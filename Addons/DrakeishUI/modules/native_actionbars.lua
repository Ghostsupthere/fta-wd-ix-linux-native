-- Keep Blizzard's enabled-bar, paging and secure button ownership. Only arrange
-- the four optional bars around the modern main row in the stock relationship:
-- two rows above it and two vertical columns at the right edge.
local addon = select(2, ...)
local pending = false
local function arrange()
    if InCombatLockdown() then pending = true; return end
    pending = false
    if not ActionButton1 then return end
    local size = ActionButton1:GetWidth()
    local gap = addon.Layout and addon.Layout:DB().actionSpacing or 7
    local scale = addon.Layout and addon.Layout:BaseScale("pUiMainBar") or (pUiMainBar and pUiMainBar:GetScale() or 1)
    local function horizontal(name, anchor, offset)
        local bar = _G[name]
        if not bar then return end
        bar:SetParent(UIParent)
        bar:SetScale(scale)
        bar:SetSize(12*size+11*gap,size)
        bar:ClearAllPoints()
        bar:SetPoint("BOTTOMLEFT",anchor,"BOTTOMLEFT",0,offset)
        for i=1,12 do
            local button = _G[name.."Button"..i]
            if button then
                button:ClearAllPoints()
                button:SetPoint("BOTTOMLEFT",bar,"BOTTOMLEFT",(i-1)*(size+gap),0)
            end
        end
    end
    horizontal("MultiBarBottomLeft",ActionButton1,size+13)
    local left = MultiBarBottomLeft and MultiBarBottomLeft:IsShown()
    horizontal("MultiBarBottomRight",ActionButton1,left and 2*(size+13) or size+13)
    local function vertical(name, anchor, point, relative, x)
        local bar = _G[name]
        if not bar then return end
        bar:SetParent(UIParent)
        bar:SetScale(scale)
        bar:SetSize(size,12*size+11*gap)
        bar:ClearAllPoints()
        bar:SetPoint(point,anchor,relative,x,0)
        bar:SetAlpha(1)
        for i=1,12 do
            local button = _G[name.."Button"..i]
            if button then
                button:ClearAllPoints()
                button:SetPoint("TOPLEFT",bar,"TOPLEFT",0,-(i-1)*(size+gap))
            end
        end
    end
    vertical("MultiBarRight",UIParent,"RIGHT","RIGHT",-16)
    vertical("MultiBarLeft",MultiBarRight or UIParent,"RIGHT","LEFT",-gap)
    if addon.Layout then addon.Layout:RefreshBars() end
end
addon.RefreshSidebarPositions = arrange
hooksecurefunc("MultiActionBar_Update",arrange)
local boot = CreateFrame("Frame")
for _,event in ipairs({"ADDON_LOADED","PLAYER_ENTERING_WORLD","PLAYER_REGEN_ENABLED","DISPLAY_SIZE_CHANGED"}) do
    boot:RegisterEvent(event)
end
boot:SetScript("OnEvent",function(_,event,name)
    if event == "ADDON_LOADED" and name ~= "DrakeishUI" then return end
    if event ~= "PLAYER_REGEN_ENABLED" or pending then arrange() end
end)
-- Drakeish is load-on-demand after login, so PLAYER_LOGIN may already be over.
-- Run the stock visibility update now as well as after each options change.
if not InCombatLockdown() then MultiActionBar_Update() else pending = true end
