local M = ...
local addon = M.addon()
local x, y = 400, 300
function GetCursorPosition() return x, y end
local function makeTip(name)
    local frame = CreateFrame("GameTooltip", name, UIParent)
    frame:SetSize(180, 80)
    frame.items = {}
    function frame:GetNumPoints() return self.point and 1 or 0 end
    function frame:GetPoint() return unpack(self.point, 1, 5) end
    function frame:ClearAllPoints() self.point = nil end
    function frame:GetOwner() return self.owner end
    function frame:GetAnchorType() return self.anchorType end
    function frame:SetAnchorType(anchor, dx, dy)
        self.anchorType, self.dx, self.dy = anchor, dx, dy
    end
    function frame:SetOwner(owner, anchor, dx, dy)
        for _, line in ipairs(self.items) do line:Hide() end
        self.items, self.owner = {}, owner
        self:SetAnchorType(anchor or "ANCHOR_LEFT", dx, dy)
        self:SetAlpha(1)
        self:SetPoint("POINT", owner, "TOP", dx or 0, dy or 0)
        if self.scripts.OnTooltipCleared then self.scripts.OnTooltipCleared(self) end
    end
    function frame:AddLine(text)
        local line = self:CreateFontString(self.name .. "TextLeft" .. (#self.items + 1))
        line:SetText(text)
        self.items[#self.items + 1] = line
    end
    function frame:NumLines() return #self.items end
    function frame:GetItem() return self.item and "Item", self.item end
    function frame:GetSpell() return self.spell and "Spell", nil, self.spell end
    function frame:GetUnit() return self.unit and "Unit", self.unit end
    frame:Hide()
    return frame
end
GameTooltip = makeTip("GameTooltip")
ShoppingTooltip1 = makeTip("ShoppingTooltip1")
function GameTooltip_SetDefaultAnchor(tip, owner) tip:SetOwner(owner, "ANCHOR_NONE") end
M.load(addon, "settings_profiles.lua")
M.load(addon, "appearance.lua")
M.load(addon, "tooltip_options.lua")
M.event("ADDON_LOADED", "DrakeishUI")
local S, tip = addon.Settings, GameTooltip
tip:SetScale(.5)
local action = CreateFrame("Button", "ActionButton3", UIParent)
tip:SetOwner(action, "ANCHOR_RIGHT", 7, 9)
tip:AddLine("Title")
tip:AddLine("Body")
tip:Show()
assert(tip:GetAnchorType() == "ANCHOR_RIGHT")
assert(S:Set("tooltip.x", 12) and S:Set("tooltip.y", -20))
assert(S:Set("tooltip.anchor", "cursor"))
assert(tip:GetAnchorType() == "ANCHOR_PRESERVE" and tip.point[4] == 734 and tip.point[5] == 560)
x = 500
M.tick(.02)
assert(tip.point[4] == 934)
assert(S:Set("tooltip.anchor", "left") and tip.point[4] == 844)
assert(S:Set("tooltip.anchor", "right") and tip.point[4] == 1024)
assert(S:Set("tooltip.anchor", "fixed") and tip.point[4] == 2380 and tip.point[5] == 0)
assert(S:Set("tooltip.anchor", "stock") and tip:GetAnchorType() == "ANCHOR_RIGHT")
assert(tip.point[4] == 7 and tip.point[5] == 9 and tip.dx == 7 and tip.dy == 9)
assert(S:Set("tooltip.actions", false) and tip:GetAlpha() == 0)
M.shift = true
M.event("MODIFIER_STATE_CHANGED", "LSHIFT", 1)
assert(tip:GetAlpha() == 1)
M.shift = false
assert(S:Set("tooltip.actions", true))
M.combat = true
assert(S:Set("tooltip.combat", true) and tip:GetAlpha() == 0)
M.combat = false
M.event("PLAYER_REGEN_ENABLED")
assert(tip:GetAlpha() == 1)
assert(S:Set("tooltip.header", 20) and tip.items[1].fontSize == 20)
assert(S:Set("tooltip.body", 16) and tip.items[2].fontSize == 16)
ShoppingTooltip1:AddLine("Comparison")
ShoppingTooltip1:Show()
assert(S:Set("tooltip.comparison", 18) and ShoppingTooltip1.items[1].fontSize == 18)
assert(S:Set("tooltip.header", 0) and tip.items[1].fontSize == 12)

tip.item = "|Hitem:4115:0:0:0:0|h[Item]|h"
assert(S:Set("tooltip.spellID", true) and tip.items[3]:GetText() == "ID: 4115")
tip.scripts.OnTooltipSetItem(tip)
assert(tip:NumLines() == 3)
assert(S:Set("tooltip.spellID", false) and not tip.items[3]:IsShown())
assert(S:Set("tooltip.spellID", true) and tip:NumLines() == 3 and tip.items[3]:IsShown())

tip:Hide()
local unrelated = CreateFrame("Button", "CustomAddonButton", UIParent)
tip:SetOwner(unrelated, "ANCHOR_LEFT")
tip:AddLine("Help")
tip:Show()
assert(S:Set("tooltip.anchor", "cursor") and tip:GetAnchorType() == "ANCHOR_LEFT")
tip:Hide()
GameTooltip_SetDefaultAnchor(tip, unrelated)
tip.item, tip.unit = nil, "target"
M.units = {target = "0xF130001234000001"}
tip:AddLine("Creature")
tip:Show()
assert(tip:GetAnchorType() == "ANCHOR_PRESERVE")
assert(S:Set("tooltip.npcID", true) and tip.items[2]:GetText() == "ID: 4660")
tip:Hide()
assert(tip:GetAnchorType() == "ANCHOR_NONE")
print("Tooltip anchors and scaling, visibility and modifiers, ID reuse and tooltip fonts passed")
