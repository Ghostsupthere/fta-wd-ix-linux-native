-- Expanded view shares the built-in macro edit box and native save path.
local panel, edit, scroll, preview, notice, findBox, replaceBox
local history, historyIndex, recordedSelection, changedAt, previewPending
local replacingHistory, originalEditor
local suggestions, suggestionRows, suggestionStart, suggestionEnd
local controls = {
    "/cast", "/use", "/castsequence", "/castrandom", "/userandom", "/stopcasting",
    "/stopmacro", "/cancelaura", "/cancelform", "/target", "/targetexact", "/targetenemy",
    "/targetfriend", "/targetenemyplayer", "/targetfriendplayer", "/targetparty", "/targetraid",
    "/targetlasttarget", "/targetlastenemy", "/targetlastfriend", "/cleartarget", "/focus",
    "/clearfocus", "/assist", "/petattack", "/petfollow", "/petstay", "/petpassive",
    "/petdefensive", "/petaggressive", "/petautocaston", "/petautocastoff",
    "/petautocasttoggle", "/equip", "/equipslot", "/equipset", "/changeactionbar",
    "/swapactionbar", "/click", "#show", "#showtooltip",
}
local selectors = {"@cursor", "@player", "@none", "@target", "@targettarget", "@focus",
    "@focustarget", "@mouseover", "@pet", "@pettarget", "@party1", "@raid1", "@arena1", "@boss1"}
local conditions = {"known:", "noknown:", "wtalent:", "glyph:", "noglyph:", "tree:", "spec:",
    "mod:", "nomod", "help", "harm", "exists", "dead", "nodead", "combat", "nocombat",
    "form:", "stance:", "group:", "equipped:", "timewalking", "notimewalking",
    "resting", "overridebar", "possessbar", "vehicleui"}

local function setNotice(text)
    notice:SetText(text or "")
end

local function flushHistory()
    if not panel or not panel:IsShown() then return end
    local text, cursor = edit:GetText(), edit:GetCursorPosition()
    local index = MacroFrame.selectedMacro
    if index ~= recordedSelection then
        recordedSelection, history, historyIndex = index, {}, 0
    end
    if history[historyIndex] and history[historyIndex].text == text then return end
    for i = #history, historyIndex + 1, -1 do history[i] = nil end
    history[#history + 1] = {text=text, cursor=cursor}
    if #history > 32 then table.remove(history, 1) end
    historyIndex = #history
end

local function resetSuggestionRows()
    for _, row in ipairs(suggestionRows) do row:Hide() end
    suggestions = nil
end

local function setText(text, cursor)
    edit:SetText(text)
    edit:SetCursorPosition(math.min(cursor, #text))
end

local function historyStep(change)
    if not MacroFrame.selectedMacro then return end
    flushHistory()
    local nextIndex = historyIndex + change
    local snapshot = history[nextIndex]
    if snapshot then
        replacingHistory = true
        setText(snapshot.text, snapshot.cursor)
        historyIndex = nextIndex
        replacingHistory = nil
    end
end

local function colored(text, color)
    return "|c" .. color .. text:gsub("|", "||") .. "|r"
end

local function refreshPreview()
    local source = edit:GetText()
    if #source > 16384 then
        preview:SetText("Shorten the draft to preview its syntax.")
        return
    end
    local bad = {}
    if ValidateMacroBody then
        for _, issue in ipairs(ValidateMacroBody(source)) do
            if issue.line > 0 then bad[issue.line] = true end
        end
    end
    local lines = {}
    for line in (source:gsub("\r\n", "\n"):gsub("\r", "\n") .. "\n"):gmatch("(.-)\n") do
        if #lines == 300 then
            lines[#lines + 1] = "Preview limited to 300 lines."
            break
        end
        local command, body = line:match("^(%s*[/%#][^%s]+)(.*)$")
        local number = #lines + 1
        local output = {colored(("%3d "):format(number), "ff888888")}
        if command then
            output[#output + 1] = colored(command, bad[number] and "ffff7474" or "ffffd169")
            local offset = 1
            while true do
                local start, finish = body:find("%b[]", offset)
                if not start then break end
                output[#output + 1] = colored(body:sub(offset, start - 1), "ffe7dcc6")
                local group = body:sub(start, finish)
                local last = 1
                for a, b in group:gmatch("()@[%w_%-]+()") do
                    output[#output + 1] = colored(group:sub(last, a - 1), "ff9ddd8d")
                    output[#output + 1] = colored(group:sub(a, b - 1), "ff86cffa")
                    last = b
                end
                output[#output + 1] = colored(group:sub(last), "ff9ddd8d")
                offset = finish + 1
            end
            local color = command:match("^%s*/[rs][uc][nr]") and "ffc0a0ef" or "ffe7dcc6"
            output[#output + 1] = colored(body:sub(offset), color)
        else
            output[#output + 1] = colored(line, "ffaaa49a")
        end
        lines[#lines + 1] = table.concat(output)
    end
    preview:SetText(table.concat(lines, "\n"))
    preview:GetParent():SetHeight(math.max(30, preview:GetStringHeight() + 14))
end

function MacroEditor_BodyChanged()
    if panel and panel:IsShown() then
        changedAt, previewPending = GetTime(), true
        if not replacingHistory then resetSuggestionRows() end
    end
end

local function showValidation()
    local issues = ValidateMacroBody(edit:GetText())
    if #issues == 0 then
        setNotice("Macro syntax looks valid.")
        return
    end
    local lines = {}
    for i = 1, math.min(3, #issues) do
        local issue = issues[i]
        lines[#lines + 1] = (issue.line > 0 and ("Line %d. "):format(issue.line) or "") .. issue.message
    end
    if #issues > 3 then lines[#lines + 1] = ("%d more."):format(#issues - 3) end
    setNotice(table.concat(lines, "  "))
end

local function findMatch(after)
    local needle = findBox:GetText()
    if needle == "" then setNotice("Enter text to find."); return end
    local text = edit:GetText()
    local start, tail = text:find(needle, math.min(after + 1, #text + 1), true)
    if not start then start, tail = text:find(needle, 1, true) end
    if not start then setNotice("Text not found."); return end
    return start, tail
end

local function findText()
    local start, tail = findMatch(edit:GetCursorPosition())
    if start then
        edit:SetFocus()
        edit:SetCursorPosition(tail)
        edit:HighlightText(start - 1, tail)
        setNotice()
    end
end

local function replaceText()
    local cursor = edit:GetCursorPosition()
    local needle = findBox:GetText()
    local text = edit:GetText()
    local start, tail
    if #needle > 0 and cursor >= #needle and text:sub(cursor - #needle + 1, cursor) == needle then
        start, tail = cursor - #needle + 1, cursor
    else
        start, tail = findMatch(cursor)
    end
    if not start then return end
    local value = replaceBox:GetText()
    local draft = text:sub(1, start - 1) .. value .. text:sub(tail + 1)
    local _, _, errorMessage = GetMacroBodyInfo(draft)
    if errorMessage then setNotice(errorMessage); return end
    flushHistory()
    setText(draft, start - 1 + #value)
    flushHistory()
    edit:SetFocus()
    setNotice()
end

local function acceptSuggestion(value)
    if not suggestions or not value then return end
    local text = edit:GetText()
    local draft = text:sub(1, suggestionStart) .. value .. text:sub(suggestionEnd + 1)
    local _, _, errorMessage = GetMacroBodyInfo(draft)
    if errorMessage then setNotice(errorMessage); return end
    flushHistory()
    setText(draft, suggestionStart + #value)
    flushHistory()
    resetSuggestionRows()
    edit:SetFocus()
end

local function complete()
    resetSuggestionRows()
    local cursor = edit:GetCursorPosition()
    local before = edit:GetText():sub(1, cursor)
    local line = before:match("([^\r\n]*)$") or ""
    local token = line:match("([@/#][%w_%-]*)$")
    local source
    if token and token:sub(1, 1) == "@" then
        source = selectors
    elseif token and line:match("^%s*[/#][%w_%-]*$") then
        source = controls
    else
        local group = line:match("%[([^%]]*)$")
        token = group and group:match("([^,%[%s]*)$")
        if token and not token:find(":", 1, true) then
            source = conditions
        else
            token = line:match("([^,:;%[%]]*)$") or ""
            if not group then token = token:match("^%s*/[%w_%-]+%s+(.*)$") or token end
            token = token:gsub("^%s+", "")
        end
    end
    local seen, matches = {}, {}
    local lower = token:lower()
    local function add(value)
        if type(value) == "string" and not seen[value] and value:lower():sub(1, #lower) == lower and #matches < 6 then
            seen[value] = true
            matches[#matches + 1] = value
        end
    end
    if source then
        for _, value in ipairs(source) do add(value) end
    else
        for tab = 1, GetNumSpellTabs() do
            local _, _, offset, count = GetSpellTabInfo(tab)
            for i = offset + 1, offset + count do
                add(GetSpellName(i, BOOKTYPE_SPELL))
                if token:match("^%d") then
                    local link = GetSpellLink(i, BOOKTYPE_SPELL)
                    if link then add(link:match("spell:(%d+)")) end
                end
                if #matches >= 6 then break end
            end
        end
        for bag = 0, 4 do
            for slot = 1, GetContainerNumSlots(bag) do
                local link = GetContainerItemLink(bag, slot)
                if link then add(GetItemInfo(link)) end
                if #matches >= 6 then break end
            end
        end
    end
    if #matches == 0 then setNotice("No matching commands, targets, or known names."); return end
    suggestionStart, suggestionEnd = cursor - #token, cursor
    suggestions = matches
    for i, value in ipairs(matches) do
        suggestionRows[i]:SetText(value)
        suggestionRows[i]:Show()
    end
    setNotice("Choose a suggestion or press Tab.")
end

local function duplicate()
    if not MacroFrame.selectedMacro or not MacroFrame_SaveMacro() then return end
    local base = GetMacroInfo(MacroFrame.selectedMacro)
    if not base then return end
    local name, serial = base .. " Copy", 2
    if #name >= 64 then name = "Macro Copy" end
    local prefix = name
    while GetMacroIndexByName(name) > 0 do
        name = prefix .. " " .. serial
        serial = serial + 1
        if #name >= 64 then prefix, name = "Macro Copy", "Macro Copy " .. serial end
    end
    local ok, index = pcall(DuplicateMacro, MacroFrame.selectedMacro, name)
    if ok and index then
        MacroFrame_SelectMacro(index)
        MacroFrame_Update()
        flushHistory()
        setNotice("Created " .. name .. ".")
    else
        setNotice(tostring(index or "Unable to copy the macro."):gsub("^.-:%d+: ", ""))
    end
end

local function closeEditor()
    if not originalEditor then return end
    local previous = originalEditor
    originalEditor, changedAt, previewPending = nil, nil, nil
    edit:ClearFocus()
    scroll:SetParent(previous.parent)
    scroll:ClearAllPoints()
    for _, point in ipairs(previous.points) do scroll:SetPoint(unpack(point)) end
    scroll:SetSize(previous.width, previous.height)
    edit:SetWidth(previous.editWidth)
    if previous.clickArea then MacroFrameTextButton:Show() end
    edit:SetScript("OnKeyDown", previous.keyDown)
    edit:SetScript("OnTabPressed", previous.tabPressed)
    resetSuggestionRows()
end

function MacroEditor_ConfirmDelete()
    if not ValidateMacroBody then MacroFrame_DeleteMacro(); return end
    local index = MacroFrame.selectedMacro
    if not index then return end
    local name, _, body = GetMacroInfo(index)
    if not name then return end
    StaticPopup_Show("FTA_CONFIRM_DELETE_MACRO", name, nil, {index=index, name=name, body=body})
end

function MacroEditor_Initialize()
    if panel or not ValidateMacroBody or not DuplicateMacro then return end
    edit, scroll = MacroFrameText, MacroFrameScrollFrame
    panel = CreateFrame("Frame", "FTAExpandedMacroEditor", MacroFrame)
    panel:SetSize(825, 480)
    panel:SetPoint("CENTER", UIParent, "CENTER", 130, 0)
    panel:SetFrameStrata("DIALOG")
    panel:EnableMouse(true)
    panel:SetMovable(true)
    panel:SetResizable(true)
    panel:SetMinResize(670, 380)
    panel:SetMaxResize(1160, 770)
    panel:SetClampedToScreen(true)
    panel:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border", tile=true, tileSize=32, edgeSize=24,
        insets={left=7,right=7,top=7,bottom=7}})
    panel:Hide()

    local drag = CreateFrame("Button", nil, panel)
    drag:SetPoint("TOPLEFT", 20, -10)
    drag:SetSize(240, 29)
    drag:RegisterForDrag("LeftButton")
    drag:SetScript("OnDragStart", function() panel:StartMoving() end)
    drag:SetScript("OnDragStop", function() panel:StopMovingOrSizing() end)
    local title = drag:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    title:SetPoint("LEFT")
    title:SetText("Macro Editor")

    local counter = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
    counter:SetPoint("TOPRIGHT", -42, -20)
    local close = CreateFrame("Button", nil, panel, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", -4, -4)
    close:SetScript("OnClick", function() panel:Hide() end)

    local resize = CreateFrame("Button", nil, panel)
    resize:SetSize(22, 22)
    resize:SetPoint("BOTTOMRIGHT", -8, 8)
    resize:SetNormalTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Up")
    resize:SetScript("OnMouseDown", function() panel:StartSizing("BOTTOMRIGHT") end)
    resize:SetScript("OnMouseUp", function() panel:StopMovingOrSizing() end)

    local syntax = CreateFrame("ScrollFrame", "FTAMacroSyntaxScroll", panel, "UIPanelScrollFrameTemplate")
    syntax:SetPoint("TOPRIGHT", -42, -73)
    local syntaxContent = CreateFrame("Frame", nil, syntax)
    syntaxContent:SetHeight(200)
    syntax:SetScrollChild(syntaxContent)
    preview = syntaxContent:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    preview:SetPoint("TOPLEFT", 5, -5)
    preview:SetPoint("TOPRIGHT", -5, -5)
    preview:SetJustifyH("LEFT")
    preview:SetJustifyV("TOP")
    local label = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
    label:SetPoint("BOTTOMLEFT", syntax, "TOPLEFT", 5, 8)
    label:SetText("Syntax preview")

    notice = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    notice:SetPoint("BOTTOMLEFT", 23, 16)
    notice:SetPoint("BOTTOMRIGHT", -40, 16)
    notice:SetJustifyH("LEFT")

    local function button(text, x, y, width, click)
        local b = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
        b:SetPoint("TOPLEFT", x, y)
        b:SetSize(width, 23)
        b:SetText(text)
        b:SetScript("OnClick", click)
        return b
    end
    local function field(x, hint)
        -- A plain backdrop instead of InputBoxTemplate: that template's middle texture did not draw for the second field.
        local box = CreateFrame("EditBox", nil, panel)
        box:SetPoint("BOTTOMLEFT", x, 42)
        box:SetSize(105, 22)
        box:SetAutoFocus(false)
        box:SetMaxBytes(256)
        box:SetFontObject("ChatFontNormal")
        box:SetTextInsets(6, 6, 0, 0)
        box:SetBackdrop({bgFile="Interface\\ChatFrame\\ChatFrameBackground",
            edgeFile="Interface\\Tooltips\\UI-Tooltip-Border", tile=true, tileSize=16, edgeSize=12,
            insets={left=3, right=3, top=3, bottom=3}})
        box:SetBackdropColor(0, 0, 0, .75)
        box:SetBackdropBorderColor(.6, .6, .6, 1)
        box:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
        local fieldLabel = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
        fieldLabel:SetPoint("BOTTOMLEFT", box, "TOPLEFT", 0, 2)
        fieldLabel:SetText(hint)
        return box
    end
    button("Save", 18, -41, 59, function() if MacroFrame_SaveMacro() then setNotice("Saved.") end end)
    button("Undo", 81, -41, 56, function() historyStep(-1) end)
    button("Redo", 142, -41, 56, function() historyStep(1) end)
    button("Check", 203, -41, 60, showValidation)
    button("Complete", 268, -41, 83, complete)
    button("Duplicate", 356, -41, 83, duplicate)

    findBox = field(26, "Find")
    replaceBox = field(202, "Replace with")
    local nextButton = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
    nextButton:SetPoint("BOTTOMLEFT", 136, 40)
    nextButton:SetSize(60, 23)
    nextButton:SetText("Next")
    nextButton:SetScript("OnClick", findText)
    findBox:SetScript("OnEnterPressed", findText)
    local replaceButton = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
    replaceButton:SetPoint("BOTTOMLEFT", 313, 40)
    replaceButton:SetSize(71, 23)
    replaceButton:SetText("Replace")
    replaceButton:SetScript("OnClick", replaceText)
    replaceBox:SetScript("OnEnterPressed", replaceText)

    suggestionRows = {}
    for i = 1, 6 do
        local row = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
        row:SetPoint("BOTTOMLEFT", 395, 43 + (6 - i) * 23)
        row:SetSize(223, 22)
        row:SetFrameLevel(panel:GetFrameLevel() + 8)
        row:SetScript("OnClick", function(self) acceptSuggestion(self:GetText()) end)
        row:Hide()
        suggestionRows[i] = row
    end

    local function layout()
        local width, height = panel:GetWidth(), panel:GetHeight() - 153
        local textWidth = math.floor(width * .54) - 37
        scroll:SetSize(textWidth, height)
        edit:SetWidth(textWidth)
        syntax:SetSize(width - textWidth - 95, height)
        syntaxContent:SetWidth(syntax:GetWidth())
        changedAt, previewPending = GetTime(), true
    end
    panel:SetScript("OnSizeChanged", function() if panel:IsShown() then layout() end end)
    panel:SetScript("OnHide", closeEditor)
    panel:SetScript("OnUpdate", function()
        if not changedAt then return end
        local elapsed = GetTime() - changedAt
        if previewPending and elapsed > .14 then
            previewPending = nil
            local characters, bytes = GetMacroBodyInfo(edit:GetText())
            if characters then
                counter:SetFormattedText("%d/%d characters (%d bytes)", characters, GetMacroBodyLimits(), bytes)
            else
                counter:SetText("Invalid text encoding")
            end
            refreshPreview()
        end
        if elapsed > .35 then
            flushHistory()
            changedAt = nil
        end
    end)
    local open = CreateFrame("Button", "FTAExpandMacroEditorButton", MacroFrame, "UIPanelButtonTemplate")
    open:SetSize(90, 22)
    open:SetPoint("LEFT", MacroEditButton, "RIGHT", 6, 0)
    open:SetFrameStrata("HIGH")
    open:SetText("Expand")
    open:SetScript("OnClick", function()
        if not MacroFrame.selectedMacro then return end
        if panel:IsShown() then panel:Hide(); return end
        originalEditor = {parent=scroll:GetParent(), width=scroll:GetWidth(), height=scroll:GetHeight(),
            editWidth=edit:GetWidth(), clickArea=MacroFrameTextButton:IsShown(),
            keyDown=edit:GetScript("OnKeyDown"), tabPressed=edit:GetScript("OnTabPressed"), points={}}
        for i = 1, scroll:GetNumPoints() do originalEditor.points[i] = {scroll:GetPoint(i)} end
        MacroFrameTextButton:Hide()
        scroll:SetParent(panel)
        scroll:ClearAllPoints()
        scroll:SetPoint("TOPLEFT", panel, "TOPLEFT", 23, -73)
        panel:Show()
        layout()
        history, historyIndex, recordedSelection = {}, 0, nil
        flushHistory()
        edit:SetScript("OnKeyDown", function(self, key)
            if IsControlKeyDown() then
                if key == "F" then findBox:SetFocus(); return
                elseif key == "H" then replaceBox:SetFocus(); return
                elseif key == "Z" then historyStep(-1); return
                elseif key == "Y" then historyStep(1); return
                elseif key == "SPACE" then complete(); return end
            end
            if originalEditor and originalEditor.keyDown then originalEditor.keyDown(self, key) end
        end)
        edit:SetScript("OnTabPressed", function()
            if suggestions then acceptSuggestion(suggestions[1])
            else edit:Insert("    ") end
        end)
    end)

    StaticPopupDialogs["FTA_CONFIRM_DELETE_MACRO"] = {
        text = "Delete the macro \"%s\"?", button1 = DELETE, button2 = CANCEL,
        OnAccept = function(_, data)
            if not data or MacroFrame.selectedMacro ~= data.index then return end
            local name, _, body = GetMacroInfo(data.index)
            if name == data.name and body == data.body then MacroFrame_DeleteMacro() end
        end,
        timeout = 0, whileDead = true, hideOnEscape = true,
    }
end
