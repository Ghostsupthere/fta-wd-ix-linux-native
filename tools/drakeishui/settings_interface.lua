-- Options that used to live in Interface > AddOns > DrakeishUI (raid frame choices, pet frame, bag layout,
-- item glow outlines, screen message positions, optional action bars), now in the DrakeishUI settings window.
-- Values stay where the Windows modules keep them (the layout database, the saved flags), so nothing migrates.
local _, addon = ...
local S, L, M = addon.Settings, addon.Layout, addon.Messages
if not (S and L) then return end

local function register(id, option)
    option.id = id
    S:Register(option)
end
local function db() return L:DB() end

-- Unit Frames > General -------------------------------------------------------------------------------------
local function updateFrames()
    if DrakeishUI_UpdateRaidFrames then DrakeishUI_UpdateRaidFrames() end
    if DrakeishUI_RefreshPartyFrames then DrakeishUI_RefreshPartyFrames() end
    if RefreshBlizzardPetFrame then RefreshBlizzardPetFrame() end
end
register("frames.compactRaid", {
    name = "Compact raid frames", type = "toggle", default = false, category = "Unit Frames", group = "Frames",
    keywords = "party raid drakeish darken", protected = true, carry = true,
    localGet = function() return not not (DrakeishUI_GetRaidFramesState and DrakeishUI_GetRaidFramesState() == 1) end,
    localSet = function(on) DrakeishUI_SetRaidFramesState(on); updateFrames() end,
})
register("frames.thinRaid", {
    name = "Thin raid frame layout", type = "toggle", default = false, category = "Unit Frames", group = "Frames",
    keywords = "party raid slim", protected = true, carry = true,
    localGet = function() return not not (DrakeishUI_GetRaidFrameStyle and DrakeishUI_GetRaidFrameStyle() == 2) end,
    localSet = function(on) DrakeishUI_SetRaidFrameStyle(on and 2 or 1); updateFrames() end,
})
register("frames.hidePet", {
    name = "Hide the pet frame", type = "toggle", default = false, category = "Unit Frames", group = "Frames",
    keywords = "pet", protected = true, carry = true,
    localGet = function() return hide_pet_frame_state == 1 end,
    localSet = function(on) hide_pet_frame_state = on and 1 or 0; updateFrames() end,
})

-- Whole-element scale, shared with the Move UI layout data ----------------------------------------------------
local function numbered(format, count)
    local names = {}
    for i = 1, count do names[i] = format:format(i) end
    return names
end
for _, spec in ipairs({
    { "player", "Player frame", { "PlayerFrame" } },
    { "target", "Target frame", { "TargetFrame" } },
    { "targettarget", "Target of Target frame", { "TargetFrameToT" } },
    { "focus", "Focus frame", { "FocusFrame" } },
    { "focustarget", "Focus Target frame", { "FocusFrameToT" } },
    { "pet", "Pet frame", { "PetFrame" } },
    { "party", "Party frames", numbered("CustomPartyFrame%d", 4) },
    { "raid", "Raid frames", { "DrakeishUI_RaidFrames" } },
    { "boss", "Boss frames", numbered("Boss%dTargetFrame", 4) },
    { "arena", "Arena enemy frames", numbered("ArenaEnemyFrame%d", 5) },
    { "mainbar", "Main action bar", { "pUiMainBar" }, "Action bars" },
    { "bottomleft", "Bottom left bar", { "MultiBarBottomLeft" }, "Action bars" },
    { "bottomright", "Bottom right bar", { "MultiBarBottomRight" }, "Action bars" },
    { "rightbar", "Right bar", { "MultiBarRight" }, "Action bars" },
    { "leftbar", "Left bar", { "MultiBarLeft" }, "Action bars" },
    { "petbar", "Pet bar", { "pUiPetBarHolder" }, "Action bars" },
    { "stancebar", "Stance / form bar", { "pUiStanceHolder" }, "Action bars" },
    { "micromenu", "Micro menu", { "pUiMicroMenu" }, "Action bars" },
    { "bagbar", "Bag buttons bar", { "pUiBagsBar" }, "Action bars" },
    { "minimap", "Minimap", { "Minimap" }, "Other" },
    { "buffs", "Buffs", { "BuffFrame" }, "Other" },
    { "castbar", "Cast bar", { "ImprovedUICastBar", "CastingBarFrame" }, "Other" },
    { "chat", "Chat", { "MinimalChatAnchor" }, "Other" },
    { "tracker", "Objective tracker", { "WatchFrame" }, "Other" },
}) do
    local key, name, names, other = unpack(spec, 1, 4)
    register("scale." .. key, {
        name = name .. " scale", type = "slider", default = 1, min = .5, max = 2, step = .05,
        category = other and "Layout" or "Unit Frames", group = other or "Frames",
        keywords = "size bigger smaller resize overall element " .. key, protected = true,
        localGet = function() return L:ElementScale(names[1]) end,
        localSet = function(value) L:SetElementScale(names, value or 1) end,
    })
end

-- Action Bars ------------------------------------------------------------------------------------------------
-- The four optional bars and the "always show empty slots" switch, handled the way the Blizzard Action Bars
-- page does: SetActionBarToggles with the fifth value, the SHOW_MULTI_ACTIONBAR_n globals, then the updates.
-- The API reports a change only once the server has answered. Until then the last request counts, whoever
-- made it: this window, a profile, or the game's own Interface options. Remembering only our own requests
-- would keep reporting a bar as off after the player had switched it on elsewhere.
local wanted = {}
local function isOn(value) return value ~= nil and value ~= false and value ~= 0 and value ~= "0" end
if SetActionBarToggles then
    hooksecurefunc("SetActionBarToggles", function(...)
        for index = 1, 4 do wanted[index] = isOn((select(index, ...))) end
    end)
end
local function currentState(index)
    if index == 5 then
        if GetCVar then return GetCVar("alwaysShowActionBars") == "1" end
        return isOn(ALWAYS_SHOW_MULTIBARS)
    end
    return isOn((select(index, GetActionBarToggles())))
end
local function toggleState(index)
    local api = currentState(index)
    local pending = wanted[index]
    if pending ~= nil then
        if api == pending then wanted[index] = nil else return pending end
    end
    return api
end
-- Blizzard's grid functions only count up from secure code, which an addon never is, so set the attribute
-- they read ourselves (allowed out of combat) and show or hide the empty slots directly.
local function showEmptySlots(on)
    for _, bar in ipairs({ "MultiBarBottomLeft", "MultiBarBottomRight", "MultiBarRight", "MultiBarLeft" }) do
        for i = 1, 12 do
            local button = _G[bar .. "Button" .. i]
            if button then
                button:SetAttribute("showgrid", on and 1 or 0)
                local normal = _G[button:GetName() .. "NormalTexture"]
                if on then
                    if normal then normal:SetVertexColor(1, 1, 1, .5) end
                    button:Show()
                elseif button.action and HasAction and not HasAction(button.action) then
                    button:Hide()
                end
            end
        end
    end
end
-- The game's Interface options keep their own copy of each box, read once at login.
local boxes = { "BottomLeft", "BottomRight", "Right", "RightTwo", "AlwaysShowActionBars" }
local function syncGameOption(index, on)
    local box = _G["InterfaceOptionsActionBarsPanel" .. boxes[index]]
    if not box then return end
    box.value = on and "1" or "0"
    if box.SetChecked then box:SetChecked(on) end
end
local function setToggle(index, on)
    local always
    if index == 5 then always = on else always = currentState(5) end
    -- One request carries all four bars, so it starts from the latest requests: the server may not have
    -- answered the previous one yet, and its old values would switch that bar back.
    local state = {}
    for bar = 1, 4 do state[bar] = toggleState(bar) and 1 or nil end
    if index <= 4 then state[index] = on and 1 or nil end
    SetActionBarToggles(state[1], state[2], state[3], state[4], always and 1 or nil)
    if index <= 4 then _G["SHOW_MULTI_ACTIONBAR_" .. index] = on and 1 or nil
    else
        ALWAYS_SHOW_MULTIBARS = on and 1 or nil
        if SetCVar then SetCVar("alwaysShowActionBars", on and "1" or "0") end
    end
    syncGameOption(index, on)
    if MultiActionBar_Update then MultiActionBar_Update() end
    if index == 5 or (always and index <= 4) then showEmptySlots(always) end
    if UIParent_ManageFramePositions then UIParent_ManageFramePositions() end
end
for index, bar in ipairs({ "bottomLeft", "bottomRight", "right", "left" }) do
    register("layout." .. bar .. ".shown", {
        name = "Show this bar", type = "toggle", default = false, category = "Action Bars", group = "Button geometry",
        keywords = "enable disable extra additional multi bar visible", protected = true, carry = true,
        localGet = function() return toggleState(index) end,
        localSet = function(on) setToggle(index, on) end,
    })
end
register("actionbars.alwaysShow", {
    name = "Always show empty action slots", type = "toggle", default = false, category = "Action Bars", group = "Optional bars",
    keywords = "always show action bars grid empty", protected = true, carry = true,
    localGet = function() return toggleState(5) end,
    localSet = function(on) setToggle(5, on) end,
})
register("actionbars.spacing", {
    name = "Optional bar button spacing", type = "slider", default = 7, min = 0, max = 16, step = 1,
    category = "Action Bars", group = "Optional bars", keywords = "gap padding", protected = true,
    localGet = function() return db().actionSpacing end,
    localSet = function(value)
        db().actionSpacing = value
        if addon.RefreshSidebarPositions then addon.RefreshSidebarPositions() end
        if MultiActionBar_Update then MultiActionBar_Update() end
    end,
})

-- Bags: the Bagster windows ----------------------------------------------------------------------------------
-- bagster_native.lua hands these to Bagster; a change only has to repaint its windows.
local function refreshBagster()
    local module = addon.BagsterModule
    if module and module.BagsterModule and module.BagsterModule.applied and addon.RefreshBagsterFrames then
        addon.RefreshBagsterFrames()
    end
end
local function bagsterOption(id, name, kind, default, group, extra)
    local option = extra or {}
    option.name, option.type, option.default = name, kind, default
    option.category, option.group = "Bags", group
    option.keywords = "bagster bag bank guild vault " .. (option.keywords or "")
    option.apply = option.apply or refreshBagster
    if option.choices then
        local valid = {}
        for _, choice in ipairs(option.choices) do valid[choice.value] = true end
        option.validate = function(value) if valid[value] then return value end end
    end
    register("bagster." .. id, option)
end
bagsterOption("itemScale", "Largest item size", "slider", 1, "Window layout",
    { min = .5, max = 1.5, step = .05, keywords = "scale", desc = "Items shrink below this to fit the window." })
bagsterOption("itemSpacing", "Space between items", "slider", 2, "Window layout", { min = 0, max = 12, step = 1, keywords = "gap" })
bagsterOption("bagBreak", "Start a new row", "dropdown", "type", "Window layout", { keywords = "separate group split",
    choices = { { value = "none", name = "Never" }, { value = "type", name = "For special bags" },
        { value = "bag", name = "For every bag" } } })
bagsterOption("money", "Money display", "dropdown", "icons", "Window layout", { keywords = "gold coins",
    choices = { { value = "icons", name = "Coin icons" }, { value = "text", name = "Text" } } })
bagsterOption("qualityFilter", "Show the quality filter buttons", "toggle", true, "Window layout", { keywords = "color rarity" })
bagsterOption("sort.fillBank", "Sorting the bank adds to its stacks from your bags", "toggle", true, "Sorting",
    { keywords = "deposit merge", desc = "When on, sorting the bank moves items out of your bags to fill matching bank stacks." })
bagsterOption("sort.reverse", "Sort items toward the end of the bags", "toggle", false, "Sorting",
    { keywords = "reverse order empty slots" })
bagsterOption("glow.quality", "Outline items by quality", "toggle", true, "Bag slot outlines", { keywords = "border rarity" })
bagsterOption("glow.quest", "Highlight quest items", "toggle", true, "Bag slot outlines", { keywords = "border" })
bagsterOption("glow.alpha", "Outline strength", "slider", 1, "Bag slot outlines",
    { min = .1, max = 1, step = .05, keywords = "opacity brightness" })
bagsterOption("glow.unusable", "Tint items you cannot use red", "toggle", false, "Bag slot outlines",
    { keywords = "unusable requirement armor weapon level",
      apply = function() if addon.RefreshUnusableItemTints then addon:RefreshUnusableItemTints() end end })

-- General > Screen messages -----------------------------------------------------------------------------------
if M then
    register("messages.enabled", {
        name = "Use custom message positions", type = "toggle", default = true, category = "General", group = "Screen messages (% from top)",
        keywords = "zone name raid warning boss emote error text",
        localGet = function() return M:DB().enabled end,
        localSet = function(on) M:DB().enabled = on; M:Apply(); M:HidePreview() end,
    })
    for _, spec in ipairs({
        { "zone", "Zone names", 18 }, { "info", "Errors and notices", 28 },
        { "raid", "Raid warnings", 38 }, { "boss", "Boss messages", 46 },
    }) do
        local key = spec[1]
        register("messages." .. key, {
            name = spec[2], type = "slider", default = spec[3], min = 5, max = 70, step = 1,
            category = "General", group = "Screen messages (% from top)", keywords = "position height zone quest progress",
            localGet = function() return M:DB()[key] end,
            localSet = function(value) M:DB()[key] = value; M:Apply(); M:Preview() end,
        })
    end
end

-- The old Interface Options pages open the new window instead. -------------------------------------------------
local function open(section) return function() if addon.Settings then addon.Settings:Open(section) end end end
L.OpenOptions = open("Action Bars")
if M then M.OpenOptions = open("General") end

-- One pointer page remains under Interface > AddOns so players who look there find the window.
local panel = CreateFrame("Frame", "DrakeishUISettingsPointer", UIParent)
panel.name = "DrakeishUI"
local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
title:SetPoint("TOPLEFT", 16, -16)
title:SetText("DrakeishUI")
local text = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
text:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -12)
text:SetPoint("RIGHT", panel, "RIGHT", -24, 0)
text:SetJustifyH("LEFT")
text:SetText("All DrakeishUI settings are in one window: raid and pet frames, action bars, bags, chat, screen messages and more. "
    .. "You can also open it with /drakeish or from the game menu.")
local openButton = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
openButton:SetSize(220, 26)
openButton:SetPoint("TOPLEFT", text, "BOTTOMLEFT", 0, -16)
openButton:SetText("Open DrakeishUI Settings")
openButton:SetScript("OnClick", function()
    if HideUIPanel and InterfaceOptionsFrame then HideUIPanel(InterfaceOptionsFrame) end
    addon.Settings:Open()
end)
if InterfaceOptions_AddCategory then InterfaceOptions_AddCategory(panel) end
