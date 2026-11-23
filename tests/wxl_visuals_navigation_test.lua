-- Run from the repo root with Lua 5.1. An optional extracted
-- OptionsFrameTemplates.lua exercises the installed category handler.
local function noop() end
local frameMethods = {
    SetScript = function(self, event, fn) self.scripts[event] = fn end,
    GetName = function(self) return self.name end,
    GetHeight = function(self) return rawget(self, "height") or 20 end,
    SetHeight = function(self, height) self.height = height end,
    GetParent = function(self) return self.parentFrame end,
    CreateFontString = function()
        return {SetPoint = noop, SetText = noop, SetWidth = noop, SetWordWrap = noop,
                SetJustifyH = noop, SetJustifyV = noop, SetSpacing = noop, GetStringWidth = function() return 40 end}
    end,
}
function CreateFrame(kind, name, parent, template)
    if template then assert(name, "template must have a global name") end
    local frame = setmetatable({name = name, parentFrame = parent, scripts = {}},
        {__index = function(_, key) return frameMethods[key] or noop end})
    if name then _G[name] = frame end
    if kind == "Slider" then
        for _, suffix in ipairs({"Text", "Low", "High"}) do
            _G[name .. suffix] = {SetText = noop}
        end
    end
    return frame
end

SlashCmdList = {}
GameMenuFrame = CreateFrame("Frame", "GameMenuFrame")
GameMenuButtonOptions = CreateFrame("Button", "GameMenuButtonOptions", GameMenuFrame)
GameMenuButtonSoundOptions = CreateFrame("Button", "GameMenuButtonSoundOptions", GameMenuFrame)
VideoOptionsFrame = CreateFrame("Frame", "VideoOptionsFrame")
VideoOptionsFramePanelContainer = CreateFrame("Frame", "VideoOptionsFramePanelContainer", VideoOptionsFrame)
VideoOptionsFrame.panelContainer = VideoOptionsFramePanelContainer
VideoOptionsEffectsPanel = CreateFrame("Frame", "VideoOptionsEffectsPanel", VideoOptionsFramePanelContainer)
VideoOptionsEffectsPanel.name = "Effects"
local resolution = CreateFrame("Frame", "Resolution", VideoOptionsFramePanelContainer)
VideoOptionsFramePanelContainer.displayedPanel = resolution
VideoOptionsFrame.categoryList = {resolution, VideoOptionsEffectsPanel}
local categoryFrame = {buttons = {}}
VideoOptionsFrame.categoryFrame = categoryFrame
function categoryFrame:update()
    self.buttons = {}
    for _, page in ipairs(VideoOptionsFrame.categoryList) do
        if not rawget(page, "hidden") then
            local button = {element = page, GetParent = function() return self end,
                LockHighlight = noop, UnlockHighlight = noop}
            function button:Click()
                if OptionsListButton_OnClick then
                    OptionsListButton_OnClick(self, "LeftButton")
                else
                    categoryFrame.selection = self.element
                    VideoOptionsFramePanelContainer.displayedPanel = self.element
                end
            end
            self.buttons[#self.buttons + 1] = button
        end
    end
end
function VideoOptionsFrame:IsShown() return self.shown end
function ShowUIPanel(frame)
    frame.shown = true
    if frame == VideoOptionsFrame then categoryFrame:update(); categoryFrame.buttons[1]:Click() end
end
function HideUIPanel(frame) frame.shown = false end
function PlaySound() end
function securecall(fn, ...) return fn(...) end
function issecure() return true end
tinsert = table.insert
if arg[1] then
    dofile(arg[1])
else
    function OptionsFrame_AddCategory(owner, panel)
        assert(owner == VideoOptionsFrame and panel.parent == VideoOptionsEffectsPanel.name)
        table.insert(owner.categoryList, panel)
    end
    function OptionsFrame_OpenToCategory(owner, name)
        assert(owner == VideoOptionsFrame and owner:IsShown() and type(name) == "string")
        for _, button in ipairs(owner.categoryFrame.buttons) do
            if button.element.name == name then button:Click(); return end
        end
    end
end
function WarcraftXL_GetEnvironmentOption() return 0 end
function WarcraftXL_GetEnvironmentOptionDefault() return 0 end
function WXL_AtmosphereStatus() return true, "" end

dofile("WoW/Source/Ui/Linux/WxlAtmosphereOptions.lua")
local panel = WXLNativeAtmospherePanel
local function verifySelection()
    assert(VideoOptionsFrame:IsShown())
    assert(categoryFrame.selection == panel)
    assert(VideoOptionsFramePanelContainer.displayedPanel == panel)
end
VideoOptionsEffectsPanel.collapsed, panel.hidden = true, true
SlashCmdList.WXLVISUALS()
verifySelection()
HideUIPanel(VideoOptionsFrame)
ShowUIPanel(GameMenuFrame)
GameMenuButtonWXLVisuals.scripts.OnClick()
verifySelection()
assert(not GameMenuFrame.shown and VideoOptionsFrame.lastFrame == GameMenuFrame)
print("Verified Visuals & Guidance category and panel from /visuals and the game menu")
