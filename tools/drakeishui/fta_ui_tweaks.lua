-- FTA tweaks for DrakeishUI. Listed last in the TOC, so the whole addon exists when this runs.
local addon = select(2, ...)

---------------------------------------------------------------------------------------------------
-- 1. Move mode shows every element, not only the ones that are visible right now.
--
-- DrakeishUI's layout editor shows a drag handle for a frame only while the frame itself is shown, so
-- the cast bar could only be moved while casting and the party frames only while in a party. A handle
-- is anchored to its frame and a frame keeps its position and size while hidden, so the handle can
-- stay up whenever move mode is on. A frame that does not exist at all (DrakeishUI's party frames
-- are not created when a competing party addon is loaded) still has no handle.
---------------------------------------------------------------------------------------------------
local L = addon.Layout
if L and type(L.RefreshBars) == "function" and not L._ftaAllHandles then
    L._ftaAllHandles = true
    local hooked = {}
    local function keepHandle(entry)
        if L.editing and not InCombatLockdown() and entry.handle then entry.handle:Show() end
    end
    local refresh = L.RefreshBars
    L.RefreshBars = function(self, ...)
        local result = refresh(self, ...)
        if InCombatLockdown() then return result end
        for name, entry in pairs(self.entries) do
            if entry.frame and not hooked[name] then
                hooked[name] = true
                -- Runs after DrakeishUI's own OnHide hook, which hides the handle.
                entry.frame:HookScript("OnHide", function() keepHandle(entry) end)
            end
            if self.editing then keepHandle(entry) end
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
