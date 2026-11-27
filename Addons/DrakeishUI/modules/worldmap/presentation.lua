-- Copyright (c) 2026 NeticSoul. Licensed under the MIT License; see LICENSE.

local addon = select(2, ...)
local WM = addon.WorldMap
local cover
local pending = {}

local function finish()
    cover:SetScript("OnUpdate", nil)
    cover:Hide()
    wipe(pending)
end

local function poll()
    for i = #pending, 1, -1 do
        local texture = pending[i]
        if not texture:IsShown() or not texture:IsTextureLoading() then
            table.remove(pending, i)
        end
    end
    if #pending == 0 then finish() end
end

function WM.RefreshMapPresentation()
    if not cover then return end
    wipe(pending)
    if not WorldMapFrame:IsShown() then finish(); return end

    -- Inspect the actual selected regions, including Blizzard's explored overlays and
    -- any dim undiscovered artwork enabled by the addon. Never infer exploration from
    -- fogdata, change server exploration bits, or load regions the UI did not select.
    for _, texture in ipairs({ WorldMapDetailFrame:GetRegions() }) do
        if texture ~= WorldMapHighlight and texture.IsTextureLoading and texture:IsShown() then
            local path = texture:GetTexture()
            if path and string.find(string.lower(path), "^interface[\\/]worldmap[\\/]")
                and texture:IsTextureLoading() then
                pending[#pending + 1] = texture
            end
        end
    end

    if #pending == 0 then finish(); return end
    -- The window and navigation stay responsive. Keep a single opaque canvas until
    -- all requested artwork has reached owner-side completion, then reveal it together.
    -- No alpha/visibility changes to the map itself: fade, fog tint and pins survive.
    cover:SetFrameLevel(WorldMapDetailFrame:GetFrameLevel() + 100)
    cover:SetFrameStrata(WorldMapFrame:GetFrameStrata())
    cover:SetScale(WorldMapDetailFrame:GetEffectiveScale() / UIParent:GetEffectiveScale())
    cover:SetScript("OnUpdate", poll)
    cover:Show()
end

function WM.BuildMapPresentation()
    if cover or not WorldMapDetailTile1.IsTextureLoading then return end
    -- Do not inherit the map's configurable movement fade: a translucent cover
    -- would leak the very partial tiles it is meant to conceal.
    cover = CreateFrame("Frame", "DrakeishWorldMapLoading", UIParent)
    cover:SetAllPoints(WorldMapDetailFrame)
    cover:EnableMouse(true)
    local background = cover:CreateTexture(nil, "BACKGROUND")
    background:SetAllPoints(cover)
    background:SetTexture(0.12, 0.10, 0.07, 1)
    local label = cover:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    label:SetPoint("CENTER", cover, "CENTER")
    label:SetText(LOADING or "Loading")
    cover:Hide()

    -- Install after the map/fog hooks. Rebuild from current bindings on every map
    -- update, including rapid zone/floor changes while an earlier request is pending.
    hooksecurefunc("WorldMapFrame_Update", WM.RefreshMapPresentation)
    WorldMapFrame:HookScript("OnShow", WM.RefreshMapPresentation)
    WorldMapFrame:HookScript("OnHide", finish)
    WM.RefreshMapPresentation()
end
