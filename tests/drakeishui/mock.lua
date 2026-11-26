local M = { frames = {}, time = 10, character = "Fixture", messages = {} }
local methods = {}
local function noop() end
local function run(self, kind, ...)
    if self.scripts[kind] then self.scripts[kind](self, ...) end
end

function methods:SetScript(kind, fn) self.scripts[kind] = fn end
function methods:HookScript(kind, fn)
    local original = self.scripts[kind]
    self.scripts[kind] = function(...)
        if original then original(...) end
        fn(...)
    end
end
function methods:RegisterEvent(event) self.events[event] = true end
function methods:UnregisterEvent(event) self.events[event] = nil end
function methods:IsShown() return self.shown end
function methods:IsVisible() return self.shown and (not self.parent or self.parent:IsVisible()) end
function methods:Show()
    if self.shown then return end
    self.shown = true
    run(self, "OnShow")
end
function methods:Hide()
    if not self.shown then return end
    self.shown = false
    run(self, "OnHide")
end
function methods:GetParent() return self.parent end
function methods:GetName() return self.name end
function methods:GetID() return self.id end
function methods:SetFrameLevel(value) self.level = value end
function methods:GetFrameLevel() return self.level or 1 end
function methods:SetText(text) self.text = text; run(self, "OnTextChanged") end
function methods:GetText() return self.text or "" end
function methods:SetFormattedText(fmt, ...) self.text = tostring(fmt):format(...) end
function methods:SetTextColor(...) self.color = { ... } end
function methods:SetFont(_, size, _) self.fontSize = size; return true end
function methods:GetFont() return "test.ttf", self.fontSize or 12, "" end
function methods:GetStringWidth() return #self:GetText() * 6 end
function methods:GetWidth() return self.width or 100 end
function methods:GetHeight() return self.height or 100 end
function methods:GetLeft() return self.left end
function methods:GetRight() return self.right end
function methods:SetWidth(w) self.width = w end
function methods:SetHeight(h) self.height = h end
function methods:SetSize(w, h) self.width, self.height = w, h end
function methods:SetChecked(on) self.checked = on end
function methods:GetChecked() return self.checked end
function methods:SetAlpha(alpha) self.alpha = alpha end
function methods:GetAlpha() return self.alpha or 1 end
function methods:GetScale() return self.scale or 1 end
function methods:GetEffectiveScale() return self.scale or 1 end
function methods:SetScale(value) self.scale = value end
function methods:SetPoint(...) self.point = { ... } end
function methods:SetAllPoints(other) self.anchor = other end
function methods:HasFocus() return self.focused end
function methods:ClearFocus()
    if self.focused then self.focused = nil; run(self, "OnEditFocusLost") end
end
function methods:SetValue(value)
    if not self.min then return end
    value = math.max(self.min, math.min(self.max, value))
    if self.value == value then return end
    self.value = value
    run(self, "OnValueChanged", value)
end
function methods:SetMinMaxValues(minimum, maximum)
    self.min, self.max = minimum, maximum
    if self.value then self:SetValue(self.value) end
end
function methods:SetValueStep() end
function methods:SetTexture(...) self.texture = { ... } end
function methods:SetTexCoord(...) self.coords = { ... } end
function methods:GetTexture() return self.texture and self.texture[1] end
function methods:CreateTexture(name, layer) return CreateFrame("Texture", name, self) end
function methods:CreateFontString(name, layer) return CreateFrame("FontString", name, self) end
function methods:GetFontString()
    self.fontString = self.fontString or self:CreateFontString()
    return self.fontString
end
function methods:GetChildren()
    local children = {}
    for _, child in ipairs(M.frames) do
        if child.parent == self and child.kind ~= "Texture" and child.kind ~= "FontString" then
            children[#children + 1] = child
        end
    end
    return unpack(children)
end
function methods:GetRegions()
    local regions = {}
    for _, child in ipairs(M.frames) do
        if child.parent == self and (child.kind == "Texture" or child.kind == "FontString") then
            regions[#regions + 1] = child
        end
    end
    return unpack(regions)
end
function methods:GetObjectType() return self.kind end
function methods:GetModel() return self.model end
function methods:ClearModel() self.model = nil end
function methods:SetUnit(unit)
    self.unit = unit
    self.requested = (self.requested or 0) + 1
    self.model = not M.loading and UnitGUID(unit) or nil
end

for _, key in ipairs({
    "ClearAllPoints", "Disable", "Enable", "EnableKeyboard", "EnableMouse", "EnableMouseWheel",
    "LockHighlight", "RegisterForDrag", "SetBackdrop", "SetBackdropBorderColor", "SetBackdropColor",
    "SetAutoFocus", "SetClampedToScreen", "SetDrawLayer", "SetFontObject", "SetFrameStrata", "SetJustifyH", "SetMaxLetters",
    "SetMovable", "SetNormalTexture", "SetShadowOffset", "SetShadowColor", "SetOrientation", "SetScrollChild", "SetTextInsets", "SetThumbTexture",
    "SetVerticalScroll", "SetVertexColor", "SetTexCoord", "StartMoving", "StopMovingOrSizing", "UnlockHighlight",
}) do methods[key] = noop end

local modelMethods = setmetatable({}, { __index = methods })
function CreateFrame(kind, name, parent)
    local meta = kind == "PlayerModel" and modelMethods or methods
    local frame = setmetatable({
        kind = kind, parent = parent, name = name, scripts = {}, events = {}, shown = true,
    }, { __index = meta })
    M.frames[#M.frames + 1] = frame
    if name then _G[name] = frame end
    return frame
end

function hooksecurefunc(object, key, handler)
    if type(object) == "string" then handler, key, object = key, object, _G end
    local original = assert(object[key], key)
    object[key] = function(...)
        local result = { original(...) }
        handler(...)
        return unpack(result)
    end
end
function InCombatLockdown() return M.combat end
function UnitName() return M.character end
function GetRealmName() return "Test Realm" end
function UnitExists(unit) return not not UnitGUID(unit) end
function UnitGUID(unit) return M.units and M.units[unit] end
function UnitIsPlayer(unit) return M.classes and M.classes[unit] ~= nil end
function UnitClass(unit) return nil, M.classes and M.classes[unit] end
function GetTime() return M.time end
function GetSpellCooldown() return M.gcdStart, M.gcdDuration end
function GetMouseFocus() return M.focus end
function IsAltKeyDown() return M.alt end
function IsControlKeyDown() return M.ctrl end
function IsShiftKeyDown() return M.shift end
function wipe(value) for key in pairs(value) do value[key] = nil end end

UIParent = CreateFrame("Frame", "UIParent")
UIParent:SetSize(1280, 800)
SlashCmdList, StaticPopupDialogs, UISpecialFrames = {}, {}, {}
YES, NO, STANDARD_TEXT_FONT, UNKNOWNOBJECT = "Yes", "No", "test.ttf", "Unknown"
tinsert = table.insert
DrakeishUI_SocialNE = {
    buttonskin = { Skin = noop }, nineslice = { AttachInset = noop }, scrollbar = { BuildCustomPixel = noop },
    chrome = { Apply = function(frame) frame.CloseButton = CreateFrame("Button", nil, frame) end },
}
function M.addon()
    return { _dir = "test/", Menu = { Close = noop, Open = function(_, entries) M.menu = entries end } }
end
function M.load(addon, path)
    assert(loadfile(M.directory .. "/" .. path))("DrakeishUI", addon)
end
function M.event(event, ...)
    for _, frame in ipairs(M.frames) do
        if frame.events[event] then run(frame, "OnEvent", event, ...) end
    end
end
function M.tick(delta)
    M.time = M.time + delta
    for _, frame in ipairs(M.frames) do
        if frame:IsVisible() then run(frame, "OnUpdate", delta) end
    end
end
function M.find(kind, text, visible)
    for _, frame in ipairs(M.frames) do
        if frame.kind == kind and (not text or frame:GetText() == text)
            and (not visible or frame:IsVisible()) then return frame end
    end
end
function M.click(text)
    local button = assert(M.find("Button", text, true), text)
    run(button, "OnClick")
end
function M.enableClip()
    modelMethods.SetClipCircle = function(self, enabled) self.clip = enabled; return true end
end
function M.capturePrint(fn)
    local original, lines = print, {}
    print = function(s) lines[#lines + 1] = s end
    local ok, err = pcall(fn)
    print = original
    assert(ok, err)
    return lines
end

return M
