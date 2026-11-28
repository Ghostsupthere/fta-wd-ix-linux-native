-- FTA addition: keep Questie's quest icon beside the nameplate. On this client Questie parents its icon to the
-- game's own plate and anchors it to that plate's left edge. The game's plate is much wider than the bar drawn
-- here, so the icon is moved next to the health bar (or next to the name when there is no bar).
local addon = LibStub("AceAddon-3.0"):GetAddon("KuiNameplates")
local mod = addon:NewModule("QuestieIcons", "AceEvent-3.0")

-- Space between the icon and the bar or name; the bar's border art sticks out about one pixel.
local GAP = 3
-- Questie's default icon offsets. Its X and Y sliders keep working as adjustments from our position.
local DEFAULT_X, DEFAULT_Y = -17, -7

local icons = {} -- game plate -> Questie's icon frame

local function questieSettings()
	local profile = Questie and Questie.db and Questie.db.profile
	return profile and profile.nameplateX or DEFAULT_X, profile and profile.nameplateY or DEFAULT_Y
end

local function place(plate, icon)
	-- Questie reuses its icon frames, so this one may belong to another plate by now.
	if icon:GetParent() ~= plate then
		icons[plate] = nil
		return
	end

	local x, y = questieSettings()
	local f = plate.kui
	icon:ClearAllPoints()

	if not f or f.nativeShown then
		-- The game's own plate is showing: Questie's own position.
		icon:SetPoint("LEFT", x, y)
		return
	end

	local dx, dy = x - DEFAULT_X, y - DEFAULT_Y
	if f.health:IsShown() then
		icon:SetPoint("RIGHT", f.health, "LEFT", dx - GAP, dy)
	else
		-- The name is centred and its region can be wider than the text.
		local half = f.name:GetStringWidth() / 2 * f:GetEffectiveScale() / icon:GetEffectiveScale()
		icon:SetPoint("RIGHT", f.name, "CENTER", dx - GAP - half, dy)
	end
end

local function placeAll()
	for plate, icon in pairs(icons) do
		place(plate, icon)
	end
end

local function placePlate(plate)
	local icon = plate and icons[plate]
	if icon then
		place(plate, icon)
	end
end

local function plateOf(f)
	for _, plate in pairs(addon.frameList) do
		if plate.kui == f then
			return plate
		end
	end
end

-- Name-only mode switches the bar off and on while the plate stays up.
local function watchHealth(plate, f)
	if f.questieIconWatched or not f.health then
		return
	end
	f.questieIconWatched = true
	local function moved()
		placePlate(plate)
	end
	hooksecurefunc(f.health, "Show", moved)
	hooksecurefunc(f.health, "Hide", moved)
end

function mod:PlateChanged(_, f)
	local plate = plateOf(f)
	if plate and icons[plate] then
		watchHealth(plate, f)
		placePlate(plate)
	end
end

function mod:HookQuestie()
	if self.questie then
		return
	end
	local loader = _G.QuestieLoader
	local nameplates = loader and loader:ImportModule("QuestieNameplate")
	local private = nameplates and nameplates.private
	if not private or not private.GetFrame then
		return
	end
	self.questie = true

	-- On 3.3.5 Questie passes the game plate itself where other clients pass a unit GUID.
	local getFrame = private.GetFrame
	private.GetFrame = function(plate, ...)
		local icon = getFrame(plate, ...)
		if icon and type(plate) == "table" then
			icons[plate] = icon
			if plate.kui then
				watchHealth(plate, plate.kui)
			end
			place(plate, icon)
		end
		return icon
	end
	-- Questie's settings re-anchor every icon.
	if nameplates.RedrawIcons then
		hooksecurefunc(nameplates, "RedrawIcons", placeAll)
	end
end

function mod:ADDON_LOADED()
	self:HookQuestie()
end

function mod:OnEnable()
	self:RegisterMessage("KuiNameplates_PostCreate", "PlateChanged")
	self:RegisterMessage("KuiNameplates_PostShow", "PlateChanged")
	-- Switching to the game's own plates sends no message.
	if not self.hooked then
		self.hooked = true
		hooksecurefunc(addon, "UseNativeNameplate", function(_, plate)
			placePlate(plate)
		end)
	end
	self:HookQuestie()
	if not self.questie then
		self:RegisterEvent("ADDON_LOADED")
	end
end
