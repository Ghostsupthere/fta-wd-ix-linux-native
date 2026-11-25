local _, addon = ...
local S = addon.Settings
local NE = DrakeishUI_SocialNE
local categories = {
    "General", "Action Bars", "Unit Frames", "Party and Raid", "Bags", "Chat", "Cast Bar",
    "Buffs", "Minimap", "Tooltips", "Character Panel", "Layout", "Profiles",
}
local WIDTH, HEIGHT, CONTENT_WIDTH = 760, 536, 514
local category = "Character Panel"
local frame, search, heading, scroll, content, reset, status
local navigation, rows, visibleRows = {}, {}, {}
local widgets = {}
local refreshing

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

local function editBox(parent, width)
    local edit = CreateFrame("EditBox", nil, parent, "InputBoxTemplate")
    edit:SetSize(width, 24)
    edit:SetAutoFocus(false)
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

local function choiceWidget(row, preview)
    local selectButton = button(row, 230)
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
    end)
    row.update = function(_, option)
        local value = read(option)
        for _, choice in ipairs(choices(option)) do
            if choice.value == value then
                selectButton:SetText(choice.name .. "   v")
                if preview then
                    local font = selectButton:GetFontString()
                    font:SetFontObject(GameFontNormal)
                    if choice.path then font:SetFont(choice.path, 12, "") end
                end
                return
            end
        end
        selectButton:SetText("Choose…")
    end
end

widgets.dropdown = function(row) choiceWidget(row, false) end
widgets.font = function(row) choiceWidget(row, true) end

widgets.slider = function(row)
    local edit = editBox(row, 58)
    edit:SetPoint("RIGHT", -8, 0)
    local slider = CreateFrame("Slider", nil, row)
    slider:SetOrientation("HORIZONTAL")
    slider:SetSize(154, 16)
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
        if not refreshing and row.option then write(row.option, value) end
    end)
    row.update = function(_, option)
        slider:SetMinMaxValues(option.min, option.max)
        slider:SetValueStep(option.step or 1)
        slider:SetValue(read(option))
        if not edit:HasFocus() then edit:SetText(tostring(read(option))) end
    end
end

widgets.colour = function(row)
    local pick = button(row, 60)
    pick:SetPoint("RIGHT", -8, 0)
    local swatch = pick:CreateTexture(nil, "ARTWORK")
    swatch:SetPoint("TOPLEFT", 8, -5)
    swatch:SetPoint("BOTTOMRIGHT", -8, 5)
    pick:SetScript("OnClick", function()
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
        swatch:SetTexture(rgb[1], rgb[2], rgb[3], 1)
    end
end

widgets.text = function(row)
    local edit = editBox(row, 226)
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
    local action = button(row, 230)
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
        row:SetSize(CONTENT_WIDTH, 32)
        row.title = label(row)
        row.title:SetPoint("LEFT", 8, 0)
        row.title:SetPoint("RIGHT", (kind == "header" or kind == "description") and -8 or -250, 0)
        widgets[kind](row)
        rows[kind][index] = row
    end
    return row
end

local function refresh()
    if not frame or not frame:IsShown() then return end
    refreshing = true
    for _, row in ipairs(visibleRows) do updateRow(row) end
    refreshing = nil

    local message = category == "Profiles" and profileMessage or ""
    if not S:IsReady() then
        message = "Settings are unavailable. Check the saved profile version and character login."
    elseif next(S.pending) then
        message = "Protected changes will apply when combat ends."
    end
    status:SetText(message)
end

local function matches(option, query)
    local text = (option.name .. " " .. option.category .. " " .. (option.keywords or "")):lower()
    for word in query:gmatch("%S+") do
        if not text:find(word, 1, true) then return false end
    end
    return true
end

local function draw()
    addon.Menu.Close()
    for _, row in ipairs(visibleRows) do
        if row.ClearEditFocus then row.ClearEditFocus() end
        row:Hide()
    end
    wipe(visibleRows)
    local query = search:GetText():lower():match("^%s*(.-)%s*$")
    local used = {}
    local function addRow(option)
        local kind = option.type
        used[kind] = (used[kind] or 0) + 1
        local row = rowFor(option, used[kind])
        row.option = option
        row:ClearAllPoints()
        row:SetPoint("TOPLEFT", content, "TOPLEFT", 0, -#visibleRows * 34)
        visibleRows[#visibleRows + 1] = row
        row:Show()
    end

    local group
    if not S:IsReady() then
        -- The status line explains why the controls are unavailable.
    elseif category == "Profiles" and query == "" then
        for _, option in ipairs(profileRows) do addRow(option) end
    else
        for _, option in ipairs(S.options) do
            if (query ~= "" and matches(option, query)) or (query == "" and option.category == category) then
                local title = option.group
                if query ~= "" then title = option.category end
                if title and title ~= group then addRow({ type = "header", name = title }) end
                group = title
                addRow(option)
            end
        end
        if query ~= "" and #visibleRows == 0 then addRow({ type = "description", name = "No matching settings." }) end
    end
    heading:SetText(query == "" and category or "Search results")
    for name, nav in pairs(navigation) do
        if query == "" and name == category then nav:LockHighlight() else nav:UnlockHighlight() end
    end
    if S:IsReady() and query == "" and category ~= "Profiles" and #visibleRows > 0 then
        reset:Show()
    else
        reset:Hide()
    end
    content:SetHeight(math.max(1, #visibleRows * 34))
    scroll:SetVerticalScroll(0)
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
    search:SetPoint("TOPRIGHT", -27, -35)
    search:SetMaxLetters(80)
    local placeholder = label(search, "GameFontDisable")
    placeholder:SetPoint("LEFT", 5, 0)
    placeholder:SetText("Search settings")
    search:SetScript("OnTextChanged", function(self)
        if self:GetText() == "" then placeholder:Show() else placeholder:Hide() end
        if frame:IsShown() then draw() end
    end)
    search:SetScript("OnEnterPressed", function(self) self:ClearFocus() end)

    local sidebar = inset(frame)
    sidebar:SetPoint("TOPLEFT", 19, -68)
    sidebar:SetPoint("BOTTOMLEFT", 19, 18)
    sidebar:SetWidth(167)
    local populated = { Profiles = true }
    for _, option in ipairs(S.options) do populated[option.category] = true end
    for index, name in ipairs(categories) do
        local nav = button(sidebar, 147, name)
        nav:SetPoint("TOPLEFT", 10, -10 - (index - 1) * 30)
        nav:SetScript("OnClick", function()
            category = name
            search:ClearFocus()
            if search:GetText() == "" then draw() else search:SetText("") end
        end)
        if not populated[name] then nav:Disable() end
        navigation[name] = nav
    end

    local pane = inset(frame)
    pane:SetPoint("TOPLEFT", sidebar, "TOPRIGHT", 8, 0)
    pane:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -18, 18)
    heading = label(pane, "GameFontNormalLarge")
    heading:SetPoint("TOPLEFT", 17, -14)
    scroll = CreateFrame("ScrollFrame", nil, pane)
    scroll:SetPoint("TOPLEFT", 10, -47)
    scroll:SetPoint("BOTTOMRIGHT", -24, 48)
    content = CreateFrame("Frame", nil, scroll)
    content:SetSize(CONTENT_WIDTH, 1)
    scroll:SetScrollChild(content)
    NE.scrollbar.BuildCustomPixel(scroll)

    reset = button(pane, 148, "Reset this section")
    reset:SetPoint("BOTTOMRIGHT", -13, 12)
    reset:SetScript("OnClick", function() S:Reset(category) end)
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
    S.onRefresh = refresh
end

function S:Open(section)
    if section then category = section end
    self:Ready()
    if not frame then build() end
    if section and search:GetText() ~= "" then
        search:SetText("")
        if frame:IsShown() then return end
    end
    if frame:IsShown() then draw() else frame:Show() end
end
