#pragma once

inline constexpr char kCharacterOrderUI[] = R"lua(
local screen, list = CharacterSelect, CharacterSelectCharacterFrame
if not screen or not list or not UpdateCharacterList or not WXL_MoveSelectedCharacter
    or WXLCharacterOrderUp then return end

local function create(name, direction, x)
    local button = CreateFrame("Button", name, list)
    button:SetSize(19, 19)
    button:SetPoint("TOPRIGHT", list, "TOPRIGHT", x, -27)
    local texture = direction < 0 and "Up" or "Down"
    local prefix = "Interface\\Buttons\\UI-ScrollBar-Scroll" .. texture .. "Button-"
    button:SetNormalTexture(prefix .. "Up")
    button:SetPushedTexture(prefix .. "Down")
    button:SetDisabledTexture(prefix .. "Disabled")
    button:SetHighlightTexture(prefix .. "Highlight", "ADD")
    button:SetScript("OnClick", function()
        local index = WXL_MoveSelectedCharacter(direction)
        if index == -1 and GlueDialog_Show then
            GlueDialog_Show("OKAY", "Could not save the character order.")
        elseif index > 0 then
            screen.selectedIndex = index
            if WORKT_CHARACTER_OFFSET and MAX_CHARACTERS_DISPLAYED then
                WORKT_CHARACTER_OFFSET = math.floor((index - 1) / MAX_CHARACTERS_DISPLAYED) * MAX_CHARACTERS_DISPLAYED
            end
            UpdateCharacterList()
        end
    end)
    return button
end

local up = create("WXLCharacterOrderUp", -1, -28)
local down = create("WXLCharacterOrderDown", 1, -7)
local function refresh()
    local index = screen.selectedIndex or 0
    if index > 1 then up:Enable() else up:Disable() end
    if index > 0 and index < GetNumCharacters() then down:Enable() else down:Disable() end
end
if hooksecurefunc then
    hooksecurefunc("UpdateCharacterList", refresh)
    if UpdateCharacterSelection then hooksecurefunc("UpdateCharacterSelection", refresh) end
end
refresh()
)lua";
