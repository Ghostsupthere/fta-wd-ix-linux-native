-- All learned professions, with no two-primary-profession limit. ProfessionLibrary
-- remains the preferred owner. Native TradeSkill/Craft frames and their dropdown
-- addons are deliberately left intact; buttons use the normal secure spell action.
local addon = select(2, ...)
local NE = DrakeishUI_SocialNE
local panel, scroll, child, empty, openButton
local rows, dirty, deferred = {}, true, false
local bookType = BOOKTYPE_SPELL or "spell"

-- Compare screen coordinates in physical pixels: the book and its list may use
-- different effective scales. On narrow displays, overlap the book's right
-- edge in front of it instead of placing the list off screen or behind it.
local function positionBeside(window, host)
    if not (window and host) then return end
    local hs = host:GetEffectiveScale()
    local ws = window:GetEffectiveScale()
    local us = UIParent:GetEffectiveScale()
    local left, right = host:GetLeft(), host:GetRight()
    local screenLeft, screenRight = UIParent:GetLeft(), UIParent:GetRight()
    window:ClearAllPoints()
    if left and right and screenLeft and screenRight and hs and ws and us then
        local width = window:GetWidth() * ws
        if screenRight * us - right * hs >= width + 12 * us then
            window:SetPoint("TOPLEFT", host, "TOPRIGHT", 8, -24)
        elseif left * hs - screenLeft * us >= width + 12 * us then
            window:SetPoint("TOPRIGHT", host, "TOPLEFT", -8, -24)
        else
            window:SetPoint("TOPRIGHT", host, "TOPRIGHT", -8, -24)
        end
    else
        window:SetPoint("TOPRIGHT", host, "TOPRIGHT", -8, -24)
    end
end

local function entries()
    local spells, result = {}, {}
    for tab = 1, (GetNumSpellTabs and GetNumSpellTabs() or 0) do
        local _, _, offset, count = GetSpellTabInfo(tab)
        for index = (offset or 0) + 1, (offset or 0) + (count or 0) do
            local link = GetSpellLink(index, bookType)
            local id = link and tonumber(link:match("spell:(%d+)"))
            local name, texture
            if id then
                local rank
                name, rank, texture = GetSpellInfo(id)
            end
            if name then spells[name] = { id = id, name = name, texture = texture } end
        end
    end
    for index = 1, (GetNumSkillLines and GetNumSkillLines() or 0) do
        local name, header, _, rank, _, _, maximum, abandonable, _, _, _, _, description = GetSkillLineInfo(index)
        local spell = name and spells[name]
        local secondary = name and spellbookSecondaryProfessionData and spellbookSecondaryProfessionData[name]
        if name and not header and maximum and maximum > 0 and
            (spell or secondary or (abandonable and description and description ~= "")) then
            result[#result + 1] = { name = name, rank = rank or 0, maximum = maximum,
                spellID = spell and spell.id, spellName = spell and spell.name,
                texture = spell and spell.texture, description = description }
        end
    end
    table.sort(result, function(a, b) return a.name:lower() < b.name:lower() end)
    return result
end
addon.NativeProfessionEntries = entries

local function build()
    if panel then return end
    panel = CreateFrame("Frame", "DrakeishUIAllProfessions", UIParent)
    panel:SetSize(340, 430)
    panel:SetPoint("CENTER")
    panel:SetFrameStrata("DIALOG")
    panel:SetClampedToScreen(true)
    panel:EnableMouse(true)
    panel:SetMovable(true)
    panel:RegisterForDrag("LeftButton")
    panel:SetScript("OnDragStart", function(self) if not InCombatLockdown() then self:StartMoving() end end)
    panel:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
    if NE and NE.panelchrome and NE.panelchrome.Apply then
        NE.panelchrome.Apply(panel, { title = "All Professions", noPortrait = true })
        -- Match the spellbook's circular portrait fill and keep it under the
        -- chrome ring. SetPortraitToTexture makes the icon circular on 3.3.5.
        local ring = panel.NineSlice or panel
        panel.portrait = ring:CreateTexture(nil, "ARTWORK")
        if NE.portrait and NE.portrait.ApplyCutout then
            NE.portrait.ApplyCutout(panel.portrait, panel)
        end
        if SetPortraitToTexture then
            SetPortraitToTexture(panel.portrait, "Interface\\Icons\\INV_Misc_Book_09")
        else
            panel.portrait:SetTexture("Interface\\Icons\\INV_Misc_Book_09")
        end
    else
        panel:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
            edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true,
            tileSize = 32, edgeSize = 32, insets = { left = 8, right = 8, top = 8, bottom = 8 } })
        local title = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
        title:SetPoint("TOP", 0, -18); title:SetText("All Professions")
        local close = CreateFrame("Button", nil, panel, "UIPanelCloseButton")
        close:SetPoint("TOPRIGHT", -4, -4)
    end
    scroll = CreateFrame("ScrollFrame", "DrakeishUIAllProfessionsScroll", panel, "UIPanelScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT", 18, -45)
    scroll:SetPoint("BOTTOMRIGHT", -36, 18)
    child = CreateFrame("Frame", nil, scroll)
    child:SetWidth(280); child:SetHeight(1)
    scroll:SetScrollChild(child)
    empty = child:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    empty:SetPoint("TOPLEFT", 8, -12)
    empty:SetText("No learned professions found.")
    panel:Hide()
    UISpecialFrames[#UISpecialFrames + 1] = "DrakeishUIAllProfessions"
end

local function refresh()
    if InCombatLockdown() then deferred = true; return end
    build()
    local data = entries()
    for i, entry in ipairs(data) do
        local row = rows[i]
        if not row then
            row = CreateFrame("Button", nil, child, "SecureActionButtonTemplate")
            row:SetSize(278, 46)
            row:RegisterForClicks("LeftButtonUp")
            row.icon = row:CreateTexture(nil, "ARTWORK")
            row.icon:SetSize(34, 34); row.icon:SetPoint("LEFT", 4, 0)
            row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
            row.text:SetPoint("LEFT", row.icon, "RIGHT", 8, 0)
            row.text:SetWidth(224); row.text:SetJustifyH("LEFT")
            row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
            row:SetScript("OnEnter", function(self)
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:SetText(self.entry.name)
                if self.entry.description then GameTooltip:AddLine(self.entry.description, 1, 1, 1, true) end
                if not self.entry.spellName then GameTooltip:AddLine("Open this gathering skill from the spellbook.", 0.7, 0.7, 0.7, true) end
                GameTooltip:Show()
            end)
            row:SetScript("OnLeave", function() GameTooltip:Hide() end)
            rows[i] = row
        end
        row.entry = entry
        row:ClearAllPoints(); row:SetPoint("TOPLEFT", 0, -(i - 1) * 48)
        row.icon:SetTexture(entry.texture or "Interface\\Icons\\INV_Misc_Book_09")
        row.text:SetText(entry.name .. "\n" .. entry.rank .. " / " .. entry.maximum)
        row:SetAttribute("type", entry.spellName and "spell" or nil)
        row:SetAttribute("spell", entry.spellName)
        row:Show()
    end
    for i = #data + 1, #rows do rows[i]:Hide() end
    child:SetHeight(math.max(1, #data * 48))
    if #data == 0 then empty:Show() else empty:Hide() end
    dirty, deferred = false, false
end

function addon:ToggleNativeProfessions()
    if InCombatLockdown() then
        if UIErrorsFrame then UIErrorsFrame:AddMessage("Professions cannot be changed during combat.", 1, 0.8, 0) end
        return
    end
    if ProfessionLibrary and type(ProfessionLibrary.Toggle) == "function" then
        -- Its public Toggle opens the *stock* spellbook. Use its existing
        -- spellbook button instead when available, preserving the optional
        -- addon's resizable panel without opening an unrelated window.
        local button = ProfessionLibrarySpellBookButton
        local click = button and button:GetScript("OnClick")
        if click then click(button) else ProfessionLibrary:Toggle() end
        local external = ProfessionLibraryFrame
        if external and external:IsShown() and not (ProfessionLibraryDB and ProfessionLibraryDB.userPlaced) then
            external:SetFrameStrata("DIALOG")
            positionBeside(external, NE and NE.spellbook and NE.spellbook.frame)
        end
        return
    end
    build()
    if panel:IsShown() then panel:Hide(); return end
    if dirty then refresh() end
    positionBeside(panel, NE and NE.spellbook and NE.spellbook.frame)
    panel:Show()
end

local function attach()
    local host = NE and NE.spellbook and NE.spellbook.frame
    if not host or openButton then return end
    openButton = CreateFrame("Button", "DrakeishUIAllProfessionsButton", host, "UIPanelButtonTemplate")
    openButton:SetSize(150, 24)
    openButton:SetPoint("BOTTOMLEFT", 42, 38)
    openButton:SetText("All Professions")
    openButton:SetScript("OnClick", function() addon:ToggleNativeProfessions() end)
end

local events = CreateFrame("Frame")
for _, event in ipairs({ "PLAYER_LOGIN", "SPELLS_CHANGED", "SKILL_LINES_CHANGED", "LEARNED_SPELL_IN_TAB", "PLAYER_REGEN_ENABLED" }) do
    events:RegisterEvent(event)
end
events:SetScript("OnEvent", function(_, event)
    if event == "PLAYER_LOGIN" then attach(); return end
    if event ~= "PLAYER_REGEN_ENABLED" then dirty = true end
    if panel and panel:IsShown() and (dirty or deferred) then refresh() end
end)
