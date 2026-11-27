-- Glyph Mastery has its own server-backed MAJOR/MINOR slots. They are not
-- Blizzard sockets 7/8 and must keep the XGLYPH addon's state/request protocol.
local NE = DrakeishUI_SocialNE
local T = NE and NE.talents
local button

local function sync()
    local owner = ExtraGlyphSlots
    if not T or not T.frame then return end
    if not button and owner and type(owner.TogglePanel) == "function" then
        button = CreateFrame("Button", "DrakeishUIGlyphMasteryButton", T.frame, "UIPanelButtonTemplate")
        button:SetSize(148, 24)
        button:SetPoint("BOTTOMLEFT", T.frame, "BOTTOMLEFT", 32, 28)
        button:SetText("Glyph Mastery")
        button:SetScript("OnClick", function()
            if InCombatLockdown() then return end
            local mastery = ExtraGlyphSlots
            if not mastery or type(mastery.TogglePanel) ~= "function" then return end
            mastery:TogglePanel()
            if mastery.frame and mastery.frame:IsShown() then
                mastery.frame:ClearAllPoints()
                mastery.frame:SetPoint("TOPLEFT", T.frame, "TOPRIGHT", 8, -8)
                mastery.frame:SetClampedToScreen(true)
            end
        end)
        T.frame:HookScript("OnHide", function()
            local mastery = ExtraGlyphSlots
            if mastery and mastery.frame and mastery.frame:IsShown() then mastery.frame:Hide() end
        end)
    end
    if button then
        if owner and T.GlyphsIsActive and T.GlyphsIsActive() and not T._inspectUnit then button:Show()
        else button:Hide() end
    end
end

local boot = CreateFrame("Frame")
boot:RegisterEvent("PLAYER_LOGIN")
boot:RegisterEvent("ADDON_LOADED")
boot:SetScript("OnEvent", function(self, event, name)
    if event == "ADDON_LOADED" and name ~= "ExtraGlyphSlots" and name ~= "DrakeishUI" then return end
    if T and not self.hooked and hooksecurefunc then
        self.hooked = true
        hooksecurefunc(T, "GlyphsApplyPaneVisibility", sync)
    end
    sync()
end)
