-- FTA addition: the "Drakeish" status bar texture. It gives nameplate health bars the look of DrakeishUI's
-- cast bar: the same streaked fill (greyed, so health colours can tint it), the same background and metal
-- border art, and the same marker riding the edge of the fill.
local addon = LibStub("AceAddon-3.0"):GetAddon("KuiNameplates")
local mod = addon:NewModule("DrakeishStyle", addon.Prototype, "AceEvent-3.0")

local TEXTURE = "Drakeish"
local MEDIA = "Interface\\AddOns\\DrakeishUI\\modules\\Kui_Nameplates\\Media\\t\\"
local CASTBAR = "Interface\\AddOns\\DrakeishUI\\modules\\DragonflightUI\\Textures\\Castbar\\"

LibStub("LibSharedMedia-3.0"):Register("statusbar", TEXTURE, MEDIA .. "drakeish-fill")

local function wanted(key)
	return addon.db.profile.general[key] ~= false
end

-- Like the cast bar, the marker is hidden on a full (or empty) bar.
local function updateMarker(health)
	local art = health.drakeishArt
	local low, high = health:GetMinMaxValues()
	local value = health:GetValue()
	if art.useMarker and high > low and value > low and value < high - (high - low) * .001 then
		art.marker:Show()
	else
		art.marker:Hide()
	end
end

-- The cast bar's marker is a pale version of its fill colour.
local function colourMarker(health, r, g, b)
	if r and g and b then
		health.drakeishArt.marker:SetVertexColor(.5 + .5 * r, .5 + .5 * g, .5 + .5 * b)
	end
end

local function create(health)
	local art = {}
	health.drakeishArt = art

	art.background = health:CreateTexture(nil, "BACKGROUND")
	art.background:SetTexture(CASTBAR .. "CastingBarBackground2")
	art.background:SetAllPoints(health)

	art.border = health:CreateTexture(nil, "OVERLAY")
	art.border:SetTexture(CASTBAR .. "CastingBarFrame2")

	art.marker = health:CreateTexture(nil, "OVERLAY")
	art.marker:SetTexture(MEDIA .. "drakeish-spark")
	art.marker:SetBlendMode("ADD")
	-- Anchored to the fill itself, so it follows Kui's smooth health animation.
	art.marker:SetPoint("CENTER", health:GetStatusBarTexture(), "RIGHT", 0, 0)

	health:HookScript("OnValueChanged", updateMarker)
	health:HookScript("OnMinMaxChanged", updateMarker)
	hooksecurefunc(health, "SetStatusBarColor", colourMarker)
end

function mod:Apply(f)
	local health = f and f.health
	if not health then
		return
	end
	local active = addon.db.profile.general.bartexture == TEXTURE
	if not health.drakeishArt then
		if not active then
			return
		end
		create(health)
	end
	local art = health.drakeishArt
	local bordered = active and wanted("drakeishborder")
	art.useMarker = active and wanted("drakeishmarker")

	local width, height = health:GetWidth(), health:GetHeight()
	-- The cast bar's proportions: a 200 x 10 bar inside a 202 x 14 border.
	local x, y = math.max(1, width * .005), math.max(1, height * .2)
	art.border:ClearAllPoints()
	art.border:SetPoint("TOPLEFT", health, "TOPLEFT", -x, y)
	art.border:SetPoint("BOTTOMRIGHT", health, "BOTTOMRIGHT", x, -y)
	art.marker:SetSize(height * 2, height * 3.2)

	if bordered then
		art.background:Show()
		art.border:Show()
	else
		art.background:Hide()
		art.border:Hide()
	end
	-- Kui's flat backing would show square corners behind the shaped art.
	if f.bg and f.bg.fill then
		f.bg.fill:SetVertexColor(0, 0, 0, bordered and 0 or .2)
	end
	colourMarker(health, health:GetStatusBarColor())
	updateMarker(health)
end

function mod:Refresh()
	for _, frame in pairs(addon.frameList) do
		self:Apply(frame.kui)
	end
end

function mod:PostEvent(_, f)
	self:Apply(f)
end

mod:AddGlobalConfigChanged("addon", {"general", "bartexture"}, nil, function(f)
	mod:Apply(f)
end)

function mod:OnEnable()
	self:RegisterMessage("KuiNameplates_PostCreate", "PostEvent")
	self:RegisterMessage("KuiNameplates_PostShow", "PostEvent")
	-- Sizes change through this, including from the settings window.
	if not self.hooked then
		self.hooked = true
		hooksecurefunc(addon, "UpdateHealthBar", function(_, f)
			mod:Apply(f)
		end)
	end
	self:Refresh()
end
