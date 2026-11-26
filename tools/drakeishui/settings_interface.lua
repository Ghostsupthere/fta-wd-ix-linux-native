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
local function copyColor(color) return { color[1], color[2], color[3] } end

-- Unit Frames > General -------------------------------------------------------------------------------------
local function updateFrames()
    if DrakeishUI_UpdateRaidFrames then DrakeishUI_UpdateRaidFrames() end
    if DrakeishUI_RefreshPartyFrames then DrakeishUI_RefreshPartyFrames() end
    if RefreshBlizzardPetFrame then RefreshBlizzardPetFrame() end
end
register("frames.compactRaid", {
    name = "Compact raid frames", type = "toggle", default = false, category = "Unit Frames", group = "Frames",
    keywords = "party raid drakeish darken", protected = true,
    localGet = function() return not not (DrakeishUI_GetRaidFramesState and DrakeishUI_GetRaidFramesState() == 1) end,
    localSet = function(on) DrakeishUI_SetRaidFramesState(on); updateFrames() end,
})
register("frames.thinRaid", {
    name = "Thin raid frame layout", type = "toggle", default = false, category = "Unit Frames", group = "Frames",
    keywords = "party raid slim", protected = true,
    localGet = function() return not not (DrakeishUI_GetRaidFrameStyle and DrakeishUI_GetRaidFrameStyle() == 2) end,
    localSet = function(on) DrakeishUI_SetRaidFrameStyle(on and 2 or 1); updateFrames() end,
})
register("frames.hidePet", {
    name = "Hide the pet frame", type = "toggle", default = false, category = "Unit Frames", group = "Frames",
    keywords = "pet", protected = true,
    localGet = function() return hide_pet_frame_state == 1 end,
    localSet = function(on) hide_pet_frame_state = on and 1 or 0; updateFrames() end,
})

-- Action Bars ------------------------------------------------------------------------------------------------
-- The four optional bars and the "always show empty slots" switch, handled the way the Blizzard Action Bars
-- page does: SetActionBarToggles with the fifth value, the SHOW_MULTI_ACTIONBAR_n globals, then the updates.
-- The API reports a change a moment later, so a value just written is remembered until it shows up.
local wanted = {}
local function currentState(index)
    if index == 5 then
        if GetCVar then return GetCVar("alwaysShowActionBars") == "1" end
        return ALWAYS_SHOW_MULTIBARS == 1 or ALWAYS_SHOW_MULTIBARS == "1"
    end
    local value = select(index, GetActionBarToggles())
    return value ~= nil and value ~= false and value ~= 0 and value ~= "0"
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
local function setToggle(index, on)
    wanted[index] = on
    local always = index == 5 and on or (index ~= 5 and currentState(5))
    local state = { GetActionBarToggles() }
    state[index] = on and 1 or nil
    SetActionBarToggles(state[1], state[2], state[3], state[4], always and 1 or nil)
    if index <= 4 then _G["SHOW_MULTI_ACTIONBAR_" .. index] = on and 1 or nil
    else
        ALWAYS_SHOW_MULTIBARS = on and 1 or nil
        if SetCVar then SetCVar("alwaysShowActionBars", on and "1" or "0") end
    end
    if MultiActionBar_Update then MultiActionBar_Update() end
    if index == 5 or (always and index <= 4) then showEmptySlots(always) end
    if UIParent_ManageFramePositions then UIParent_ManageFramePositions() end
end
for index, bar in ipairs({ "bottomLeft", "bottomRight", "right", "left" }) do
    register("layout." .. bar .. ".shown", {
        name = "Show this bar", type = "toggle", default = false, category = "Action Bars", group = "Button geometry",
        keywords = "enable disable extra additional multi bar visible", protected = true,
        localGet = function() return toggleState(index) end,
        localSet = function(on) setToggle(index, on) end,
    })
end
register("actionbars.alwaysShow", {
    name = "Always show empty action slots", type = "toggle", default = false, category = "Action Bars", group = "Optional bars",
    keywords = "always show action bars grid empty", protected = true,
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

-- Bags > Layout ----------------------------------------------------------------------------------------------
local function refreshBags() if L.RefreshBags then L:RefreshBags() end end
local function bagOption(key, name, kind, default, extra, after)
    local option = extra or {}
    option.name, option.type, option.default = name, kind, default
    option.category, option.protected = "Bags", true
    option.group = option.group or "Combined bags"
    option.localGet = function() return db()[key] end
    option.localSet = function(value) db()[key] = value; if after then after() end; refreshBags() end
    register("layout." .. key, option)
end
bagOption("combined", "Combine carried bags into one window", "toggle", false, { keywords = "backpack single window" })
bagOption("bagScale", "Bag scale", "slider", 1, { min = .5, max = 1.5, step = .05 })
bagOption("bagSpacing", "Space between separate bags", "slider", 14, { min = 4, max = 40, step = 1, group = "Separate bags",
    keywords = "gap distance windows only when bags are not combined" })
bagOption("columns", "Combined bag columns", "slider", 20, { min = 8, max = 28, step = 1 })
bagOption("bagSlotSpacing", "Combined bag item spacing", "slider", 4, { min = 0, max = 16, step = 1 })
bagOption("sortMode", "Sort and group by", "dropdown", "Type", {
    choices = { { value = "Type", name = "Type" }, { value = "Name", name = "Name" }, { value = "Quality", name = "Quality" } },
    keywords = "sort order",
})
bagOption("bagAutoSort", "Auto-sort when bags open", "toggle", false, { keywords = "sort" })
bagOption("bagGroupSpacing", "Space between item groups", "toggle", true, { keywords = "group gap" },
    function() if addon.Inventory and addon.Inventory.InvalidateLayout then addon.Inventory:InvalidateLayout() end end)

-- Bags > Quality: item glow outlines -------------------------------------------------------------------------
local function refreshItems() if addon.Inventory and addon.Inventory.FilterItems then addon.Inventory:FilterItems() end end
local function glowOption(id, option)
    option.category, option.group, option.protected = "Bags", "Item glow outlines", true
    option.keywords = (option.keywords or "") .. " outline border category"
    register(id, option)
end
glowOption("bagGlow.enabled", {
    name = "Show category glow outlines", type = "toggle", default = false,
    localGet = function() return db().bagGlow end,
    localSet = function(on) db().bagGlow = on; refreshItems() end,
})
for _, category in ipairs(L.outlineTypes or {}) do
    local key = category[1]
    local slug = key:lower():gsub("%s+", "_")
    glowOption("bagGlow.type." .. slug, {
        name = key, type = "toggle", default = true,
        localGet = function() return db().bagGlowTypes[key] end,
        localSet = function(on) db().bagGlowTypes[key] = on; refreshItems() end,
    })
    glowOption("bagGlow.color." .. slug, {
        name = key .. " color", type = "colour", default = { category[2], category[3], category[4] },
        localGet = function() return copyColor(db().bagGlowColors[key]) end,
        localSet = function(color) db().bagGlowColors[key] = copyColor(color); refreshItems() end,
    })
end
glowOption("bagGlow.alpha", {
    name = "Outline brightness", type = "slider", default = .65, min = .1, max = 1, step = .05,
    localGet = function() return db().bagGlowAlpha end,
    localSet = function(value) db().bagGlowAlpha = value; refreshItems() end,
})

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
L.OpenOptions, L.OpenBagOptions, L.OpenOutlineOptions = open("Action Bars"), open("Bags"), open("Bags")
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
