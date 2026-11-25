-- FTA tweaks for DrakeishUI. Listed last in the TOC, so the whole addon exists when this runs.
local addon = select(2, ...)

---------------------------------------------------------------------------------------------------
-- 1. Move mode (Move UI / "/drakeish move"): every element, a grid, snapping and coordinates.
--
-- a) DrakeishUI's layout editor shows a drag handle for a frame only while the frame itself is shown,
--    so the cast bar could only be moved while casting and the party frames only while in a party. A
--    handle is anchored to its frame and a frame keeps its position and size while hidden, so the
--    handle stays up whenever move mode is on. A frame that does not exist at all (the party frames
--    are not created when a competing party addon is loaded) still has no handle.
-- b) A grid behind the handles, like ElvUI's: square cells, the centre lines red, on the background
--    strata so it never covers an interface element.
-- c) Dragging snaps to the grid lines and to the edges and centres of the other elements (a cyan guide
--    line shows what it snapped to). Hold Shift to move freely.
-- d) A panel under the top bar shows the selected element's X and Y (its centre, measured from the
--    screen centre, in interface pixels). Type values or use the arrow buttons (Shift = 10 pixels).
---------------------------------------------------------------------------------------------------
local L = addon.Layout
if L and type(L.RefreshBars) == "function" and not L._ftaMoveMode then
    L._ftaMoveMode = true
    local CELLS_ACROSS = 32   -- grid cells across the screen width
    local SNAP = 8            -- snap distance in interface pixels

    local function uiScale() return UIParent:GetEffectiveScale() end
    -- Centre and size of an element in UIParent units; nil while the frame has no position yet.
    local function rect(entry)
        local frame = entry.frame
        local cx, cy = frame:GetCenter()
        if not cx then return nil end
        local r = frame:GetEffectiveScale() / uiScale()
        return cx * r, cy * r, frame:GetWidth() * r, frame:GetHeight() * r
    end
    local function place(entry, cx, cy)
        local r = uiScale() / entry.frame:GetEffectiveScale()
        entry.frame:ClearAllPoints()
        entry.frame:SetPoint("CENTER", UIParent, "BOTTOMLEFT", cx * r, cy * r)
    end
    -- Same record the stock drag saves: the centre as a fraction of the screen.
    local function save(entry, cx, cy)
        local db = L:DB()
        local saved = db.bars[entry.name]
        if type(saved) ~= "table" then saved = {} end
        saved.x = cx / UIParent:GetWidth()
        saved.y = cy / UIParent:GetHeight()
        db.bars[entry.name] = saved
        L:ApplyBar(entry.frame)
    end

    -- Grid ----------------------------------------------------------------------------------------
    local grid
    local function gridStep() return UIParent:GetWidth() / CELLS_ACROSS end
    local function buildGrid()
        local width, height = UIParent:GetWidth(), UIParent:GetHeight()
        if grid and grid.width == width and grid.height == height then return grid end
        if grid then grid:Hide() end
        grid = CreateFrame("Frame", nil, UIParent)
        grid.width, grid.height = width, height
        grid:SetAllPoints(UIParent)
        grid:SetFrameStrata("BACKGROUND")
        local step = gridStep()
        local function line(centre, horizontal, red)
            local texture = grid:CreateTexture(nil, "BACKGROUND")
            if red then texture:SetTexture(1, 0, 0, 0.55) else texture:SetTexture(0, 0, 0, 0.45) end
            if horizontal then
                texture:SetPoint("TOPLEFT", grid, "BOTTOMLEFT", 0, centre + 0.5)
                texture:SetPoint("BOTTOMRIGHT", grid, "BOTTOMRIGHT", 0, centre - 0.5)
            else
                texture:SetPoint("TOPLEFT", grid, "TOPLEFT", centre - 0.5, 0)
                texture:SetPoint("BOTTOMRIGHT", grid, "BOTTOMLEFT", centre + 0.5, 0)
            end
        end
        for i = 0, CELLS_ACROSS do line(i * step, false, i == CELLS_ACROSS / 2) end
        line(height / 2, true, true)
        for i = 1, math.floor(height / 2 / step) do
            line(height / 2 + i * step, true, false)
            line(height / 2 - i * step, true, false)
        end
        return grid
    end

    -- Guides (shown while a drag is snapped to another element) -----------------------------------
    local guides
    local function showGuides(gx, gy)
        if not guides then
            guides = CreateFrame("Frame", nil, UIParent)
            guides:SetAllPoints(UIParent)
            guides:SetFrameStrata("DIALOG")
            guides.v = guides:CreateTexture(nil, "OVERLAY")
            guides.v:SetTexture(0, 0.85, 1, 0.9)
            guides.h = guides:CreateTexture(nil, "OVERLAY")
            guides.h:SetTexture(0, 0.85, 1, 0.9)
        end
        if gx then
            guides.v:ClearAllPoints()
            guides.v:SetPoint("TOPLEFT", guides, "TOPLEFT", gx - 0.5, 0)
            guides.v:SetPoint("BOTTOMRIGHT", guides, "BOTTOMLEFT", gx + 0.5, 0)
            guides.v:Show()
        else guides.v:Hide() end
        if gy then
            guides.h:ClearAllPoints()
            guides.h:SetPoint("TOPLEFT", guides, "BOTTOMLEFT", 0, gy + 0.5)
            guides.h:SetPoint("BOTTOMRIGHT", guides, "BOTTOMRIGHT", 0, gy - 0.5)
            guides.h:Show()
        else guides.h:Hide() end
        guides:Show()
    end
    local function hideGuides() if guides then guides:Hide() end end

    -- Snapping ------------------------------------------------------------------------------------
    -- Moves the centre c of a box of the given size so one of its edges or its centre lands on a
    -- target line (other elements first, then the grid) within SNAP. Returns the centre and the line
    -- of another element it snapped to, if any.
    local function snapAxis(c, size, lines, base)
        local step = gridStep()
        local best, shift, guide = SNAP + 0.001, nil, nil
        for _, offset in ipairs({ -size / 2, 0, size / 2 }) do
            local p = c + offset
            for _, target in ipairs(lines) do
                local d = math.abs(target - p)
                if d < best then best, shift, guide = d, target - p, target end
            end
            local target = base + math.floor((p - base) / step + 0.5) * step
            local d = math.abs(target - p)
            if d < best then best, shift, guide = d, target - p, nil end
        end
        if shift then return c + shift, guide end
        return c, nil
    end
    local function otherLines(moving)
        local xs, ys = {}, {}
        for _, entry in pairs(L.entries) do
            if entry ~= moving then
                local cx, cy, w, h = rect(entry)
                if cx and w > 1 and h > 1 then
                    xs[#xs + 1] = cx - w / 2; xs[#xs + 1] = cx; xs[#xs + 1] = cx + w / 2
                    ys[#ys + 1] = cy - h / 2; ys[#ys + 1] = cy; ys[#ys + 1] = cy + h / 2
                end
            end
        end
        return xs, ys
    end

    -- Coordinate panel ----------------------------------------------------------------------------
    local coords
    local function selectedEntry() return L.entries[L.selected] end
    local function refreshCoords()
        if not coords or not coords:IsShown() then return end
        local entry = selectedEntry()
        if not entry then
            coords.label:SetText("Click an element")
            return
        end
        coords.label:SetText(entry.label)
        local cx, cy = rect(entry)
        if not cx then return end
        if not coords.x:HasFocus() then coords.x:SetText(math.floor(cx - UIParent:GetWidth() / 2 + 0.5)) end
        if not coords.y:HasFocus() then coords.y:SetText(math.floor(cy - UIParent:GetHeight() / 2 + 0.5)) end
    end
    local function moveTo(x, y)   -- offsets from the screen centre; nil keeps that axis
        local entry = selectedEntry()
        if not entry or InCombatLockdown() then return end
        local cx, cy = rect(entry)
        if not cx then return end
        local W, H = UIParent:GetWidth(), UIParent:GetHeight()
        save(entry, W / 2 + (x or (cx - W / 2)), H / 2 + (y or (cy - H / 2)))
        refreshCoords()
    end
    local function nudge(dx, dy)
        local entry = selectedEntry()
        if not entry then return end
        local cx, cy = rect(entry)
        if not cx then return end
        local W, H = UIParent:GetWidth(), UIParent:GetHeight()
        local n = IsShiftKeyDown() and 10 or 1
        moveTo(cx - W / 2 + dx * n, cy - H / 2 + dy * n)
    end
    local function buildCoords()
        if coords then return coords end
        coords = CreateFrame("Frame", "DrakeishUILayoutCoords", UIParent)
        coords:SetSize(420, 66)
        coords:SetPoint("TOP", UIParent, "TOP", 0, -82)
        coords:SetFrameStrata("DIALOG")
        coords:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 14 })
        coords:SetBackdropColor(.03, .08, .12, .98)
        coords.label = coords:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        coords.label:SetPoint("TOPLEFT", 14, -10)
        local hint = coords:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        hint:SetPoint("TOPRIGHT", -14, -12)
        hint:SetText("Snaps to the grid and elements. Shift: free")
        local function box(x)
            local edit = CreateFrame("EditBox", nil, coords)
            edit:SetSize(64, 22)
            edit:SetPoint("TOPLEFT", x, -30)
            edit:SetAutoFocus(false)
            edit:SetFontObject("GameFontHighlight")
            edit:SetTextInsets(8, 8, 0, 0)
            edit:SetMaxLetters(6)
            edit:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
                edgeSize = 10, insets = { left = 2, right = 2, top = 2, bottom = 2 } })
            edit:SetBackdropColor(0, 0, 0, 0.6)
            edit:SetBackdropBorderColor(0.6, 0.6, 0.6, 1)
            edit:SetScript("OnEscapePressed", function(self) self:ClearFocus(); refreshCoords() end)
            return edit
        end
        local function caption(text, x)
            local fs = coords:CreateFontString(nil, "OVERLAY", "GameFontNormal")
            fs:SetPoint("TOPLEFT", x, -36)
            fs:SetText(text)
        end
        caption("X", 14); coords.x = box(30)
        caption("Y", 106); coords.y = box(122)
        coords.x:SetScript("OnEnterPressed", function(self)
            local value = tonumber(self:GetText()); self:ClearFocus()
            if value then moveTo(value, nil) else refreshCoords() end
        end)
        coords.y:SetScript("OnEnterPressed", function(self)
            local value = tonumber(self:GetText()); self:ClearFocus()
            if value then moveTo(nil, value) else refreshCoords() end
        end)
        -- One arrow texture, turned to point each way (corner order: UL, LL, UR, LR).
        local function arrow(x, dx, dy, texture, ...)
            local button = CreateFrame("Button", nil, coords, "UIPanelButtonTemplate")
            button:SetSize(32, 24)
            button:SetPoint("TOPLEFT", x, -29)
            local icon = button:CreateTexture(nil, "OVERLAY")
            icon:SetTexture(texture)
            icon:SetSize(18, 18)
            icon:SetPoint("CENTER", 0, 0)
            if select("#", ...) > 0 then icon:SetTexCoord(...) end
            button:SetScript("OnClick", function() nudge(dx, dy) end)
        end
        local UP, DOWN = "Interface\\Buttons\\Arrow-Up-Up", "Interface\\Buttons\\Arrow-Down-Up"
        arrow(206, -1, 0, UP, 1, 0, 0, 0, 1, 1, 0, 1)    -- left: the up arrow turned counter-clockwise
        arrow(242, 1, 0, UP, 0, 1, 1, 1, 0, 0, 1, 0)     -- right: turned clockwise
        arrow(278, 0, 1, UP)
        arrow(314, 0, -1, DOWN)
        return coords
    end

    -- Dragging ------------------------------------------------------------------------------------
    local drag
    local driver = CreateFrame("Frame")
    driver:SetScript("OnUpdate", function()
        if not drag then return end
        local entry = drag.entry
        local scale = uiScale()
        local mx, my = GetCursorPosition()
        local cx = drag.startX + mx / scale - drag.mouseX
        local cy = drag.startY + my / scale - drag.mouseY
        local gx, gy
        if not IsShiftKeyDown() then
            local _, _, w, h = rect(entry)
            local xs, ys = otherLines(entry)
            cx, gx = snapAxis(cx, w or 0, xs, 0)
            cy, gy = snapAxis(cy, h or 0, ys, UIParent:GetHeight() / 2)
        end
        drag.cx, drag.cy = cx, cy
        place(entry, cx, cy)
        showGuides(gx, gy)
        refreshCoords()
    end)
    local function endDrag(commit)
        local d = drag
        drag = nil
        L.moving = nil
        hideGuides()
        if not d then return end
        local entry = d.entry
        if entry.name == "MinimalChatAnchor" then entry.frame:SetUserPlaced(false) end
        if InCombatLockdown() then L.pending = true; return end
        if commit then save(entry, d.cx or d.startX, d.cy or d.startY) end
        refreshCoords()
    end
    local function installDrag(entry)
        local handle = entry.handle
        if not handle or entry._ftaDrag then return end
        entry._ftaDrag = true
        handle:SetScript("OnDragStart", function()
            if InCombatLockdown() then return end
            L:Select(entry.name)
            local cx, cy = rect(entry)
            if not cx then return end
            local mx, my = GetCursorPosition()
            L.moving = entry
            entry.frame:SetClampedToScreen(true)
            drag = { entry = entry, startX = cx, startY = cy, mouseX = mx / uiScale(), mouseY = my / uiScale() }
        end)
        handle:SetScript("OnDragStop", function() endDrag(true) end)
    end

    -- Hooks into the stock editor -----------------------------------------------------------------
    local hooked = {}
    local function keepHandle(entry)
        if L.editing and not InCombatLockdown() and entry.handle then entry.handle:Show() end
    end
    local refresh = L.RefreshBars
    L.RefreshBars = function(self, ...)
        local result = refresh(self, ...)
        if InCombatLockdown() then return result end
        for name, entry in pairs(self.entries) do
            installDrag(entry)
            if entry.frame and not hooked[name] then
                hooked[name] = true
                -- Runs after DrakeishUI's own OnHide hook, which hides the handle.
                entry.frame:HookScript("OnHide", function() keepHandle(entry) end)
            end
            if self.editing then keepHandle(entry) end
        end
        return result
    end
    local select_ = L.Select
    L.Select = function(self, ...)
        local result = select_(self, ...)
        refreshCoords()
        return result
    end
    local setEditing = L.SetEditing
    L.SetEditing = function(self, ...)
        local result = setEditing(self, ...)
        if self.editing then
            buildGrid():Show()
            buildCoords():Show()
            refreshCoords()
        else
            endDrag(false)
            if grid then grid:Hide() end
            if coords then coords:Hide() end
        end
        return result
    end
end

---------------------------------------------------------------------------------------------------
-- 2. Up and Down arrows in the chat box step through the messages you sent earlier.
--
-- The stock chat boxes ignore the arrow keys unless Alt is held (ignoreArrows), and keep 32 lines.
-- Arrow keys without Alt go to the history, the same as ElvUI; Left and Right move the cursor.
---------------------------------------------------------------------------------------------------
local HISTORY_LINES = 5
local function chatHistory()
    for i = 1, NUM_CHAT_WINDOWS or 10 do
        local box = _G["ChatFrame" .. i .. "EditBox"]
        if box and box.SetAltArrowKeyMode then
            box:SetAltArrowKeyMode(false)
            if box.SetHistoryLines then box:SetHistoryLines(HISTORY_LINES) end
        end
    end
end
chatHistory()
local watcher = CreateFrame("Frame")
watcher:RegisterEvent("PLAYER_ENTERING_WORLD")
watcher:SetScript("OnEvent", function(self)
    self:UnregisterEvent("PLAYER_ENTERING_WORLD")
    chatHistory()
end)

---------------------------------------------------------------------------------------------------
-- 3. Totem bar buttons without a spell show no tooltip instead of a Lua error.
--
-- The stock tooltip code for the shaman totem bar's Summon and Recall buttons passes self.spellId to
-- GameTooltip:SetSpellByID. That field is nil until the button has been updated, or when the shaman
-- does not know the spell, and SetSpellByID raises "Invalid spell ID" for it (reported while cycling
-- the action bar pages).
---------------------------------------------------------------------------------------------------
local stockTotemTooltip = MultiCastSpellButton_SetTooltip
if type(stockTotemTooltip) == "function" and not _G.FTA_TotemTooltipGuard then
    _G.FTA_TotemTooltipGuard = true
    MultiCastSpellButton_SetTooltip = function(self, ...)
        if type(self) ~= "table" or type(self.spellId) ~= "number" then
            if type(self) == "table" then self.UpdateTooltip = nil end
            if GameTooltip and GameTooltip:GetOwner() == self then GameTooltip:Hide() end
            return
        end
        return stockTotemTooltip(self, ...)
    end
end

---------------------------------------------------------------------------------------------------
-- 4. Channel notices the client has no text for no longer raise a Lua error.
--
-- Stock ChatFrame_MessageEventHandler builds a channel notice from _G["CHAT_" .. notice .. "_NOTICE"]
-- and string.format errors when that global does not exist. The server sends NOT_IN_LFG when the
-- player enters a city (reported on entering Stormwind), and the 3.3.5 client has no text for it.
-- ElvUI drops that one notice in its own chat module; with DrakeishUI there is no such handling.
-- A message filter runs before the format, so any notice without text is dropped quietly.
---------------------------------------------------------------------------------------------------
if type(ChatFrame_AddMessageEventFilter) == "function" and not _G.FTA_ChannelNoticeGuard then
    _G.FTA_ChannelNoticeGuard = true
    ChatFrame_AddMessageEventFilter("CHAT_MSG_CHANNEL_NOTICE", function(_, _, notice)
        if type(notice) ~= "string" or _G["CHAT_" .. notice .. "_NOTICE"] == nil then
            return true
        end
        return false
    end)
end
