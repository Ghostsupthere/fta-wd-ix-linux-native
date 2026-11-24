-- Linux client compatibility for DrakeishUI's bag windows.
--
-- The FrameXML shipped in this client's patch archive has a ContainerFrame_GenerateFrame that
-- ignores its fourth argument (layoutOnly): it always shows the frame and appends it to
-- ContainerFrame1.bags. DrakeishUI calls it with layoutOnly=true to put a bag window back to its
-- stock layout when combined bags turn off or the last bag closes. On this client that call
-- re-opens the bag, the combined window is rebuilt, and no bag can be closed any more.
--
-- This file loads before the rest of the addon, so DrakeishUI's own hooksecurefunc calls chain
-- onto the wrapper. A closed window needs no layout now, because the next real open lays it out
-- completely. An open window is laid out normally and the duplicate list entry is removed.
-- A client whose generator already honours layoutOnly is unaffected.
-- Vendor window: stock MerchantFrame_OnShow calls OpenBackpack() and MerchantFrame_OnHide calls
-- CloseBackpack() unless the backpack was already open. DrakeishUI makes OpenBackpack (through
-- ToggleBackpack) open every carried bag in the one combined window, but stock CloseBackpack only
-- hides the backpack frame. The other bags stay open, the combined window stays on screen showing
-- only them, and the backpack items are gone from it. When the combined window is up and the
-- backpack was not open before the vendor, close all carried bags the way the window's own close
-- button does.
local stockCloseBackpack = CloseBackpack
if type(stockCloseBackpack) == "function" and not DrakeishUI_CloseBackpackShim then
    DrakeishUI_CloseBackpackShim = true
    CloseBackpack = function(...)
        local host = DrakeishUICombinedBags
        if host and host:IsShown() and ContainerFrame1 and ContainerFrame1.backpackWasOpen == nil then
            for bag = 0, 4 do CloseBag(bag) end
            return
        end
        return stockCloseBackpack(...)
    end
end

local generate = ContainerFrame_GenerateFrame
if type(generate) ~= "function" or DrakeishUI_LayoutOnlyShim then return end
DrakeishUI_LayoutOnlyShim = true

local function dedupe(list)
    local seen, kept = {}, {}
    for _, name in ipairs(list) do
        if not seen[name] then seen[name] = true; kept[#kept + 1] = name end
    end
    for i = 1, #list do list[i] = kept[i] end
end

ContainerFrame_GenerateFrame = function(frame, size, id, layoutOnly)
    if not layoutOnly then return generate(frame, size, id) end
    if not frame:IsShown() then return end
    generate(frame, size, id, true)
    if ContainerFrame1 and ContainerFrame1.bags then dedupe(ContainerFrame1.bags) end
end
