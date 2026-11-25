local _, addon = ...
local art = pUiMainBarArt
if not art or not addon.RefreshEndCapPositions then return end

local watched = {}
local prefixes = { "ActionButton", "BonusActionButton" }
local driver = CreateFrame("Frame")
driver:Hide()

local function request()
    driver:Show()
end

function addon.GetEndCapOffsets(leftOffset, rightOffset)
    local artLeft, artRight = art:GetLeft(), art:GetRight()
    local scale = art:GetEffectiveScale()
    if not artLeft or not artRight or scale <= 0 then return leftOffset, rightOffset end

    local left, right
    for button in pairs(watched) do
        if button:IsVisible() then
            local buttonLeft, buttonRight = button:GetLeft(), button:GetRight()
            if buttonLeft and buttonRight then
                local ratio = button:GetEffectiveScale() / scale
                left = math.min(left or math.huge, buttonLeft * ratio)
                right = math.max(right or -math.huge, buttonRight * ratio)
            end
        end
    end
    if not left then return leftOffset, rightOffset end
    -- The configured offsets refer to the outside of the art, not its inner edge.
    -- Retain any existing clearance while reserving at least the full art width.
    return left-artLeft-math.max(-leftOffset, MainMenuBarLeftEndCap:GetWidth()),
           right-artRight+math.max(rightOffset, MainMenuBarRightEndCap:GetWidth())
end

local function watchButtons()
    -- Include extension buttons beyond the twelve stock slots if they exist.
    for _, prefix in ipairs(prefixes) do
        for i = 1, math.max(24, NUM_ACTIONBAR_BUTTONS or 12) do
            local button = _G[prefix .. i]
            if button and not watched[button] then
                watched[button] = true
                button:HookScript("OnShow", request)
                button:HookScript("OnHide", request)
                button:HookScript("OnSizeChanged", request)
                hooksecurefunc(button, "SetPoint", request)
            end
        end
    end
end

driver:SetScript("OnUpdate", function(self)
    self:Hide()
    addon.RefreshEndCapPositions()
end)
for _, event in ipairs({ "ADDON_LOADED", "PLAYER_ENTERING_WORLD", "ACTIONBAR_PAGE_CHANGED",
    "UPDATE_BONUS_ACTIONBAR", "DISPLAY_SIZE_CHANGED", "UI_SCALE_CHANGED" }) do
    driver:RegisterEvent(event)
end
driver:SetScript("OnEvent", function()
    watchButtons()
    request()
end)
art:HookScript("OnSizeChanged",request)
watchButtons()
request()
