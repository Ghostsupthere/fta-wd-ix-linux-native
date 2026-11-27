-- FTA addition: a row of debuffs under (or over) each nameplate. The client gives every visible plate a unit
-- token ("nameplate3"), so debuffs are read from the unit itself; nothing is guessed from the combat log.
local addon = LibStub("AceAddon-3.0"):GetAddon("KuiNameplates")
local mod = addon:NewModule("Debuffs", "AceEvent-3.0")
local kui = LibStub("Kui-1.0")

local INTERVAL = .1
local MAX_TOKENS = 64
local MINE = { player = true, pet = true, vehicle = true }
local TYPE_COLOURS = {
	Magic = { .2, .6, 1 },
	Curse = { .6, 0, 1 },
	Disease = { .6, .4, 0 },
	Poison = { 0, .6, 0 },
}
local PHYSICAL = { .8, 0, 0 }
local PLAIN = { 0, 0, 0 }

local settings
local found = {} -- reused between scans

local function createButton(container)
	local button = CreateFrame("Frame", nil, container)

	button.border = button:CreateTexture(nil, "BACKGROUND")
	button.border:SetTexture(kui.m.t.solid)
	button.border:SetAllPoints(button)

	button.icon = button:CreateTexture(nil, "ARTWORK")
	button.icon:SetPoint("TOPLEFT", button, "TOPLEFT", 1, -1)
	button.icon:SetPoint("BOTTOMRIGHT", button, "BOTTOMRIGHT", -1, 1)
	button.icon:SetTexCoord(.07, .93, .07, .93)

	button.cooldown = CreateFrame("Cooldown", nil, button, "CooldownFrameTemplate")
	button.cooldown:SetAllPoints(button.icon)
	button.cooldown:SetReverse(true)

	-- The text sits above the sweep.
	button.textHolder = CreateFrame("Frame", nil, button)
	button.textHolder:SetAllPoints(button)
	button.time = button.textHolder:CreateFontString(nil, "OVERLAY")
	button.time:SetPoint("CENTER", button, "CENTER", 0, 0)
	button.count = button.textHolder:CreateFontString(nil, "OVERLAY")
	button.count:SetPoint("BOTTOMRIGHT", button, "BOTTOMRIGHT", 2, -2)

	return button
end

local function styleButton(button, level)
	local size = settings.size
	button:SetSize(size, size)
	button:SetFrameLevel(level)
	button.cooldown:SetFrameLevel(level + 1)
	button.textHolder:SetFrameLevel(level + 2)
	button.time:SetFont(addon.font, math.max(6, math.floor(size * .55 + .5)), "OUTLINE")
	button.count:SetFont(addon.font, math.max(6, math.floor(size * .5 + .5)), "OUTLINE")
	button.styleRevision = mod.revision
	button.sweepStart = nil
end

local function clear(f)
	local container = f and f.debuffs
	if container then
		container:Hide()
		container.unit = nil
	end
end

-- Under the bar the row moves below the cast bar while one is showing; over it, it clears the name.
local function anchor(f, container)
	local x, y = settings.x, settings.y
	container:ClearAllPoints()
	if settings.position == "above" then
		local top = f.health
		if f.name and f.name:IsShown() and (addon.db.profile.text.nameanchorpoint or "TOP") == "TOP" then
			top = f.name
		end
		container:SetPoint("BOTTOM", top, "TOP", x, 3 + y)
	else
		local castbar = f.castbar
		if castbar and castbar:IsShown() then
			container:SetPoint("TOP", castbar.name or castbar.bg, "BOTTOM", x, -3 + y)
		else
			container:SetPoint("TOP", f.health, "BOTTOM", x, -5 + y)
		end
	end
end

local function byTimeLeft(a, b)
	if a.expires ~= b.expires then
		return a.expires < b.expires
	end
	return a.index < b.index
end

-- The debuffs to show for a unit, soonest to expire first; permanent ones last.
local function scan(unit)
	local count = 0
	for index = 1, 40 do
		local name, _, icon, stacks, kind, duration, expires, caster = UnitDebuff(unit, index)
		if not name then
			break
		end
		if settings.filter ~= "mine" or MINE[caster] then
			count = count + 1
			local entry = found[count] or {}
			found[count] = entry
			entry.icon, entry.stacks, entry.kind, entry.index = icon, stacks or 0, kind, index
			entry.duration = duration or 0
			entry.expires = (entry.duration > 0 and expires) or math.huge
		end
	end
	for index = #found, count + 1, -1 do
		found[index] = nil
	end
	table.sort(found, byTimeLeft)
	return count
end

local function timeText(left)
	if left >= 3600 then
		return math.ceil(left / 3600) .. "h"
	elseif left >= 60 then
		return math.ceil(left / 60) .. "m"
	end
	return tostring(math.max(0, math.ceil(left)))
end

local function fill(button, entry, now)
	button.icon:SetTexture(entry.icon)

	local colour = settings.colour and (TYPE_COLOURS[entry.kind] or PHYSICAL) or PLAIN
	button.border:SetVertexColor(colour[1], colour[2], colour[3], 1)

	if settings.stacks and entry.stacks > 1 then
		button.count:SetText(entry.stacks)
		button.count:Show()
	else
		button.count:Hide()
	end

	local timed = entry.duration > 0
	if timed and settings.timer then
		button.time:SetText(timeText(entry.expires - now))
		button.time:Show()
	else
		button.time:Hide()
	end

	if timed and settings.sweep then
		local start = entry.expires - entry.duration
		-- Setting an unchanged cooldown again would restart its animation.
		if button.sweepStart ~= start or button.sweepDuration ~= entry.duration then
			button.sweepStart, button.sweepDuration = start, entry.duration
			button.cooldown:SetCooldown(start, entry.duration)
		end
		button.cooldown:Show()
	else
		button.sweepStart = nil
		button.cooldown:Hide()
	end

	button:Show()
end

function mod:UpdatePlate(f, unit)
	if not settings.enabled or not f:IsShown() or f.nameonly or not f.health then
		return clear(f)
	end
	local count = math.min(scan(unit), settings.max)
	if count == 0 then
		return clear(f)
	end

	local container = f.debuffs
	if not container then
		container = CreateFrame("Frame", nil, f)
		container.buttons = {}
		f.debuffs = container
	end
	container.unit = unit
	container:SetSize(count * settings.size + (count - 1) * settings.spacing, settings.size)
	anchor(f, container)

	local now = GetTime()
	local level = f:GetFrameLevel() + 3
	for index = 1, count do
		local button = container.buttons[index]
		if not button then
			button = createButton(container)
			container.buttons[index] = button
		end
		if button.styleRevision ~= self.revision then
			styleButton(button, level)
		end
		button:ClearAllPoints()
		button:SetPoint("LEFT", container, "LEFT", (index - 1) * (settings.size + settings.spacing), 0)
		fill(button, found[index], now)
	end
	for index = count + 1, #container.buttons do
		container.buttons[index]:Hide()
	end
	container:Show()
end

function mod:Scan()
	if not C_NamePlate then
		return
	end
	local remaining = #C_NamePlate.GetNamePlates()
	for number = 1, MAX_TOKENS do
		if remaining <= 0 then
			break
		end
		local unit = "nameplate" .. number
		local plate = C_NamePlate.GetNamePlateForUnit(unit)
		if plate then
			remaining = remaining - 1
			if plate.kui then
				self:UpdatePlate(plate.kui, unit)
			end
		end
	end
end

-- Called after any debuff setting changes.
function mod:Refresh()
	settings = self.db.profile
	self.revision = (self.revision or 0) + 1
	if not settings.enabled then
		for _, frame in pairs(addon.frameList) do
			clear(frame.kui)
		end
	else
		self:Scan()
	end
end

-- A recycled plate must not show the previous unit's debuffs, even for a moment.
function mod:PlateChanged(_, f)
	clear(f)
end

function mod:OnInitialize()
	self.db = addon.db:RegisterNamespace(self.moduleName, {profile = {
		enabled = true,
		filter = "mine",
		position = "below",
		size = 16,
		max = 6,
		spacing = 2,
		x = 0,
		y = 0,
		timer = true,
		sweep = true,
		stacks = true,
		colour = true,
	}})
	settings = self.db.profile
	self.revision = 1
end

function mod:OnEnable()
	settings = self.db.profile
	self:RegisterMessage("KuiNameplates_PostShow", "PlateChanged")
	self:RegisterMessage("KuiNameplates_PostHide", "PlateChanged")

	if not self.driver then
		self.driver = CreateFrame("Frame")
		self.driver.elapsed = 0
		self.driver:SetScript("OnUpdate", function(driver, elapsed)
			driver.elapsed = driver.elapsed + elapsed
			if driver.elapsed >= INTERVAL then
				driver.elapsed = 0
				if settings.enabled then
					mod:Scan()
				end
			end
		end)
	end
	-- Kui replaces this when fonts change.
	if not self.hooked then
		self.hooked = true
		hooksecurefunc(addon, "UpdateAllFonts", function()
			mod:Refresh()
		end)
	end
end
