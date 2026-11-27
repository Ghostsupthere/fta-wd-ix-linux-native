-- FTA addition: separate fonts for enemy names and health text, each with a Bold switch. Bold swaps in a
-- bold font file, so it only does something for the fonts listed in BOLD.
local addon = LibStub("AceAddon-3.0"):GetAddon("KuiNameplates")
local mod = addon:NewModule("TextFonts", "AceEvent-3.0")
local LSM = LibStub("LibSharedMedia-3.0")

local MEDIA = "Interface\\AddOns\\DrakeishUI\\modules\\Kui_Nameplates\\Media\\f\\"

LSM:Register("font", "Expressway", "Interface\\AddOns\\DrakeishUI\\assets\\expressway.ttf")

local BOLD = {
	-- The game has no bold Arial Narrow; Nimbus Sans Narrow Bold is a close match.
	["Arial Narrow"] = MEDIA .. "nimbus-sans-narrow-bold.otf",
	["Expressway"] = MEDIA .. "expressway-bold.ttf",
}

-- part is "name" or "health"
function mod:Path(part)
	local general = addon.db.profile.general
	local font = general[part .. "font"] or addon.db.profile.fonts.options.font
	if general[part .. "bold"] and BOLD[font] then
		return BOLD[font]
	end
	return LSM:Fetch("font", font, true) or addon.font
end

local function setFont(region, path)
	if not region then
		return false
	end
	local current, size, flags = region:GetFont()
	if current == path then
		return false
	end
	region:SetFont(path, size, flags)
	return true
end

function mod:Apply(f)
	if not f or not f.name then
		return
	end
	local renamed = setFont(f.name, self:Path("name"))
	setFont(f.health and f.health.p, self:Path("health"))
	if renamed and f:IsShown() and f.oldName then
		-- Long names are shortened to fit, which depends on the font.
		if not f.player then
			f:SetName()
		end
		addon:UpdateSelectedName(f)
	end
end

function mod:Refresh()
	for _, frame in pairs(addon.frameList) do
		self:Apply(frame.kui)
	end
end

function mod:PostEvent(_, f)
	self:Apply(f)
end

function mod:OnEnable()
	self:RegisterMessage("KuiNameplates_PostCreate", "PostEvent")
	self:RegisterMessage("KuiNameplates_PostShow", "PostEvent")
	if not self.hooked then
		self.hooked = true
		-- Kui resets every string to the shared font through this.
		hooksecurefunc(addon, "UpdateAllFonts", function()
			mod:Refresh()
		end)
	end
	self:Refresh()
end
