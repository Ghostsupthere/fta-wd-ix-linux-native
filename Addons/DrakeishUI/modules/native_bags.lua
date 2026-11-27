-- Modern decoration on the client's real, separate bag windows. The stock Lua
-- generator owns capacity, layout, slot IDs, clicks, tooltips and item updates.
local NE = DrakeishUI_SocialNE
local addon = select(2,...)
local menu
local function showMenu(button)
    if not UIDropDownMenu_Initialize then return end
    if not menu then menu = CreateFrame("Frame", "DrakeishUINativeBagMenu", UIParent, "UIDropDownMenuTemplate") end
    UIDropDownMenu_Initialize(menu, function()
        local function entry(label, action)
            local info = UIDropDownMenu_CreateInfo()
            info.text, info.notCheckable, info.func = label, true, action
            UIDropDownMenu_AddButton(info)
        end
        entry("Bag settings",function() if addon.Layout then addon.Layout:OpenBagSettings() end end)
        entry("Search, filter and sort",function() if addon.Inventory then addon.Inventory:ShowTools() end end)
        entry("Sort carried bags",function() if addon.Inventory then addon.Inventory:StartSort() end end)
        local id=button:GetParent():GetID()
        if id and id>=0 and id<=4 and addon.Layout then
            local excluded=addon.Layout:DB().excludeSort[id]
            entry(excluded and "Include this bag in sorting" or "Exclude this bag from sorting",function()
                addon.Layout:DB().excludeSort[id]=not excluded
            end)
        end
        entry(OPEN_ALL_BAGS or "Open all bags", OpenAllBags)
        entry(CLOSE_ALL_BAGS or "Close all bags", CloseAllBags)
        entry("Modern UI layout",function() if addon.Layout then addon.Layout:OpenOptions() end end)
        if SlashCmdList and SlashCmdList.REAGENTBANKUI then
            entry("Reagent bank", function() SlashCmdList.REAGENTBANKUI("") end)
        end
        if SlashCmdList and SlashCmdList.GEARBANKCLIENT then
            entry("Gear bank", function() SlashCmdList.GEARBANKCLIENT("") end)
        end
    end, "MENU")
    ToggleDropDownMenu(1, nil, menu, button, 0, 0)
end
local function skin(frame)
    if not frame or not NE or not NE.panelchrome then return end
    local name = frame:GetName()
    if not name then return end
    if not frame._duiBagChrome then
        frame._duiBagChrome = true
        frame.TitleText = _G[name.."Name"]
        -- Reuse the stock title. PanelChrome otherwise creates a second title
        -- on a separate band that survives when the stock header is hidden.
        frame.Title = frame.TitleText
        NE.panelchrome.Apply(frame, {title = GetBagName(frame:GetID()) or BACKPACK_TOOLTIP,
            layout = "HeldBagLayout", noPortrait = true, noCloseButton = true})
        local close = _G[name.."CloseButton"]
        if close then NE.panelchrome.ModernizeCloseButton(close, {anchor=false}) end
        local button = CreateFrame("Button", nil, frame)
        frame._duiBagMenu = button
        button:SetSize(20,20)
        button:SetPoint("TOPRIGHT",frame,"TOPRIGHT",-24,-3)
        -- This is the same working cog texture used by the modern character panel.
        button:SetNormalTexture("Interface\\WorldMap\\Gear_64Grey")
        button:SetHighlightTexture("Interface\\WorldMap\\Gear_64Grey")
        button:SetScript("OnClick", showMenu)
        button:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self,"ANCHOR_LEFT")
            GameTooltip:SetText("Bag settings and sorting")
            GameTooltip:Show()
        end)
        button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    end
    -- Include the metal/portrait overhang when packing separate windows.
    frame._duiBagInsets = {left=13,right=4,top=16,bottom=3}
    -- The chrome's title band renders at parent level + 11. Keep both header
    -- controls above it, including after a container frame is recycled.
    local close = _G[name.."CloseButton"]
    local level = frame:GetFrameLevel() + 20
    if close then close:SetFrameLevel(level) end
    if frame._duiBagMenu then frame._duiBagMenu:SetFrameLevel(level) end
    for _,suffix in ipairs({"BackgroundTop","BackgroundBottom","Background1Slot",
            "BackgroundMiddle1","BackgroundMiddle2","BackgroundMiddle3","BackgroundMiddle4"}) do
        local texture = _G[name..suffix]
        if texture then texture:Hide() end
    end
    local portrait = _G[name.."Portrait"]
    if portrait then
        -- Match the actual 60px cutout in the reused portrait-metal corner.
        -- The 40px stock portrait was sitting below/right of that ring.
        portrait:ClearAllPoints()
        portrait:SetPoint("TOPLEFT",frame,"TOPLEFT",-5,8)
        portrait:SetSize(60,60)
        portrait:SetDrawLayer("ARTWORK")
        portrait:SetTexCoord(0,1,0,1)
        portrait:Show()
        if frame:GetID() == 0 then
            if SetPortraitToTexture then SetPortraitToTexture(portrait,"Interface\\Buttons\\Button-Backpack-Up")
            else portrait:SetTexture("Interface\\Buttons\\Button-Backpack-Up") end
        else
            SetBagPortraitTexture(portrait,frame:GetID())
        end
        local hit = _G[name.."PortraitButton"]
        if hit then hit:ClearAllPoints();hit:SetAllPoints(portrait) end
    end
    local title = _G[name.."Name"]
    if title then
        title:ClearAllPoints()
        title:SetPoint("TOPLEFT",frame,"TOPLEFT",48,-12)
        title:SetWidth(math.max(20,frame:GetWidth()-94))
    end
    if frame._duiCombined and addon.SetNativeBagCombined then addon.SetNativeBagCombined(frame,true) end
end

addon.SkinNativeBag=skin
function addon.SetNativeBagCombined(frame,enabled)
    frame._duiCombined=enabled and true or nil
    local name=frame:GetName()
    local regions={frame.NineSlice,frame.Bg,frame.TopTileStreaks,frame.Title,frame.TitleText,
        frame.TitleContainer,frame._neTitleBand,frame._neTitle,_G[name.."Name"],
        _G[name.."Portrait"],_G[name.."PortraitButton"],_G[name.."CloseButton"],frame._duiBagMenu}
    -- Some chrome fields are absent; ipairs would stop at the first nil.
    if enabled then
        frame._duiChromeVisibility=frame._duiChromeVisibility or {}
        for _,region in pairs(regions) do
            if frame._duiChromeVisibility[region]==nil then frame._duiChromeVisibility[region]=region:IsShown() end
            region:Hide()
        end
    elseif frame._duiChromeVisibility then
        for region,shown in pairs(frame._duiChromeVisibility) do if shown then region:Show() else region:Hide() end end
        frame._duiChromeVisibility=nil
    end
    if enabled then local money=_G[name.."MoneyFrame"];if money then money:Hide() end end
end

hooksecurefunc("ContainerFrame_GenerateFrame",skin)
for i=1,NUM_CONTAINER_FRAMES do
    local frame = _G["ContainerFrame"..i]
    if frame and frame:IsShown() then skin(frame) end
end
