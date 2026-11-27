-- Copyright (c) 2026 NeticSoul. Licensed under the MIT License; see LICENSE.

local addon = select(2, ...)
local WM = addon.WorldMap

-- The previous replacement hid both models and loaded assets/Minimap/poi-player,
-- which was absent from the installed package. Keep the native model/facing and
-- zone/floor visibility owner instead of hiding it behind missing replacement art.
local function restore()
    if not WorldMapPlayer then return false end
    local level=math.max(WorldMapPlayer:GetFrameLevel(),(WORLDMAP_POI_FRAMELEVEL or 10)+100)
    WorldMapPlayer:SetFrameLevel(level)
    local found=false
    for _,name in ipairs({"PlayerArrowFrame","PlayerArrowEffectFrame"}) do
        local model=_G[name]
        if model then model:SetAlpha(1);model:SetFrameLevel(level+1);found=true end
    end
    return found
end
WM.RefreshPlayerArrow=restore
function WM.BuildPlayerArrow()
    if WM.nativeArrowDriver then return end
    local driver=CreateFrame("Frame",nil,WorldMapFrame)
    WM.nativeArrowDriver=driver
    local function refresh()
        restore()
        -- Native model frames are lazy. Retry while visible only until both
        -- arrive, then remove the update callback entirely.
        driver:SetScript("OnUpdate",function(self)
            restore()
            if PlayerArrowFrame and PlayerArrowEffectFrame then self:SetScript("OnUpdate",nil) end
        end)
    end
    WorldMapFrame:HookScript("OnShow",refresh)
    refresh()
end
