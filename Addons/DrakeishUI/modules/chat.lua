local addon = select(2,...);
local config = addon.config;
local pairs = pairs;
local gsub = string.gsub;
local UIParent = UIParent;
local hooksecurefunc = hooksecurefunc;
local _G = _G;


local f = CreateFrame("Frame")
f:RegisterEvent("PLAYER_LOGIN")
f:RegisterEvent("PLAYER_ENTERING_WORLD")
f:RegisterEvent("DISPLAY_SIZE_CHANGED")
f:RegisterEvent("UI_SCALE_CHANGED")

-- SavedVariables setup
if type(MinimalChatDB) ~= "table" then MinimalChatDB = {} end
if MinimalChatDB.view ~= "combat" then
    MinimalChatDB.view = "chat"
end

-- Anchor frame for moving chat
local chatAnchor = CreateFrame("Frame", "MinimalChatAnchor", UIParent)
chatAnchor:SetSize(300, 100)
chatAnchor:SetMovable(true)
chatAnchor:SetUserPlaced(false)
chatAnchor:EnableMouse(false)
chatAnchor:SetClampedToScreen(true)
chatAnchor:RegisterForDrag("LeftButton")

-- Border visuals
chatAnchor:SetBackdrop({
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 12,
    insets = { left = 3, right = 3, top = 3, bottom = 3 }
})
chatAnchor:SetBackdropColor(0, 0, 0, 0)
chatAnchor:SetBackdropBorderColor(0, 0, 0, 0)
-- Keep the positioning frame alive when locked. The integrated layout editor
-- supplies its mouse handle; hiding this frame used to move the status footer.
chatAnchor:SetPoint("BOTTOMLEFT",UIParent,"BOTTOMLEFT",16,40)
chatAnchor:Show()

-- Register slash command AFTER everything is loaded
SLASH_MINIMALCHAT1 = "/chatanchor"
SlashCmdList["MINIMALCHAT"] = function()
    if addon.Layout then addon.Layout:SetEditing(not addon.Layout.editing) end
end

local chromeStyled = false
local applyingView = false
local applyingGeometry = false
local geometryPending = {}
local geometryDriver = CreateFrame("Frame", "DrakeishUIChatPlacement")

-- Fully hide UI chrome elements (once)
local function kill(frame)
    if not frame or frame.drakeishKilled then return end
    frame.drakeishKilled = true
    frame:Hide()
    frame.Show = function() end
    frame:SetAlpha(0)
    frame:SetScale(0.001)
    frame:SetParent(chatAnchor)
end

local function GetCombatLogFrame()
    return _G.ChatFrame2 or _G.COMBATLOG
end

local function GetActiveChatFrame()
    if MinimalChatDB.view == "combat" then
        return GetCombatLogFrame() or ChatFrame1
    end
    return ChatFrame1
end

local function ApplyChatGeometry(chat)
    applyingGeometry = true
    chat:ClearAllPoints()
    chat:SetPoint("BOTTOMLEFT", chatAnchor, "BOTTOMLEFT", 0, 0)
    chat:SetScale(chatAnchor:GetScale())
    chat:SetSize(chatAnchor:GetWidth(), chatAnchor:GetHeight())
    -- Docked secondary windows cannot accept SetUserPlaced in the native API.
    -- Their geometry still follows our anchor; the stock dock owns their flags.
    if chat:IsMovable() or chat:IsResizable() then
        chat:SetUserPlaced(false)
    end
    applyingGeometry = false
end

local function QueueChatGeometry(chat)
    if applyingGeometry or geometryPending[chat] then return end
    geometryPending[chat] = true
    geometryDriver:SetScript("OnUpdate", function(self)
        self:SetScript("OnUpdate", nil)
        for frame in pairs(geometryPending) do
            geometryPending[frame] = nil
            ApplyChatGeometry(frame)
        end
    end)
end

local function PlaceChatFrame(chat)
    if not chat then return end
    ApplyChatGeometry(chat)
    if chat._drakeishGeometryOwned then return end
    chat._drakeishGeometryOwned = true
    -- Preserve the stock movement capabilities: its primary-window restore
    -- calls SetUserPlaced even while locked. The anchor and hidden chat chrome
    -- own movement here; changing both flags breaks that restore contract.
    chat:EnableMouse(true)
    chat:EnableMouseWheel(true)
    if chat.SetHyperlinksEnabled then chat:SetHyperlinksEnabled(true) end
    chat:SetClampedToScreen(false)
    chat:SetJustifyH("LEFT")
    -- Stock dock changes can make several writes in one call. Restore only the
    -- geometry, once after the call completes, without changing the active tab.
    for _, method in ipairs({"ClearAllPoints", "SetPoint", "SetAllPoints", "SetSize",
        "SetWidth", "SetHeight", "SetScale", "SetUserPlaced"}) do
        hooksecurefunc(chat, method, QueueChatGeometry)
    end
end

-- The typing line sits above the chat, with the stock input border removed.
-- Combat log uses its own edit box, which Blizzard anchors to the bottom and
-- paints with UI-ChatInputBorder. Put every box on the same spot and strip it.
local EDITBOX_BORDERS = { "Left", "Right", "Mid", "FocusLeft", "FocusRight", "FocusMid" }

local function StripEditBoxTextures(editBox)
    if not editBox then return end
    local name = editBox.GetName and editBox:GetName()
    if name then
        for _, suffix in ipairs(EDITBOX_BORDERS) do
            local tex = _G[name .. suffix]
            if tex then
                tex:SetTexture(nil)
                tex:Hide()
                tex.Show = function() end
            end
        end
    end
    for i = 1, select("#", editBox:GetRegions()) do
        local region = select(i, editBox:GetRegions())
        if region and region.GetObjectType and region:GetObjectType() == "Texture" then
            region:SetTexture(nil)
            region:Hide()
        end
    end
end

local function PlaceEditBox(editBox)
    if not editBox then return end
    StripEditBoxTextures(editBox)
    editBox:SetScale(chatAnchor:GetScale())
    editBox:ClearAllPoints()
    editBox:SetPoint("BOTTOMLEFT", chatAnchor, "TOPLEFT", 0, 2)
    editBox:SetPoint("BOTTOMRIGHT", chatAnchor, "TOPRIGHT", 0, 2)
    if not editBox._drakeishPlaced then
        editBox._drakeishPlaced = true
        editBox:HookScript("OnShow", function(self)
            PlaceEditBox(self)
        end)
    end
end

local function PlaceAllChatEditBoxes()
    local count = NUM_CHAT_WINDOWS or 10
    for i = 1, count do
        PlaceEditBox(_G["ChatFrame" .. i .. "EditBox"])
    end
end

local function StyleChatChromeOnce()
    if chromeStyled then return end
    chromeStyled = true

    kill(ChatFrameMenuButton)
    kill(QuickJoinToastButton)
    kill(ChatFrameChannelButton)
    kill(ChatFrameToggleVoiceDeafenButton)
    kill(ChatFrameToggleVoiceMuteButton)
    kill(FriendsMicroButton)
    kill(CombatLogQuickButtonFrame)
    kill(CombatLogQuickButtonFrame_Custom)

    for i = 1, NUM_CHAT_WINDOWS do
        kill(_G["ChatFrame"..i.."Tab"])
        kill(_G["ChatFrame"..i.."ScrollBar"])
        kill(_G["ChatFrame"..i.."UpButton"])
        kill(_G["ChatFrame"..i.."DownButton"])
        kill(_G["ChatFrame"..i.."ButtonFrame"])

        local chat = _G["ChatFrame"..i]
        if chat then
            for _, region in ipairs({chat:GetRegions()}) do
                if region.GetTexture and region:GetTexture() then
                    region:SetTexture(nil)
                end
            end
        end
    end

    local editBox = ChatFrame1EditBox
    if editBox then
        for i = 1, select("#", editBox:GetRegions()) do
            local region = select(i, editBox:GetRegions())
            if region.GetTexture and region:GetTexture() then
                region:SetTexture(nil)
            end
        end
    end
end

-- Lightweight show/hide only — never call from OnShow of the frames it toggles
local function ApplyChatView()
    if applyingView then return end
    applyingView = true

    local combat = GetCombatLogFrame()
    local hidden = (chat_state == 1)
    local wantCombat = (MinimalChatDB.view == "combat")

    if hidden then
        if ChatFrame1 then ChatFrame1:Hide() end
        if combat then combat:Hide() end
        if ChatFrame1EditBox then ChatFrame1EditBox:Hide() end
        if _G.UnifiedInfoFrame then _G.UnifiedInfoFrame:Hide() end
        applyingView = false
        return
    end

    if wantCombat and combat then
        -- Prefer Blizzard dock select so combat log stays the active window
        if FCF_SelectDockFrame then
            pcall(FCF_SelectDockFrame, combat)
        end
        if ChatFrame1 then ChatFrame1:Hide() end
        PlaceChatFrame(combat)
        combat:SetAlpha(1)
        combat:Show()
    else
        if FCF_SelectDockFrame and ChatFrame1 then
            pcall(FCF_SelectDockFrame, ChatFrame1)
        end
        if combat then combat:Hide() end
        PlaceChatFrame(ChatFrame1)
        ChatFrame1:SetAlpha(1)
        ChatFrame1:Show()
    end

    if _G.UnifiedInfoFrame then
        _G.UnifiedInfoFrame:Show()
    end

    PlaceAllChatEditBoxes()
    applyingView = false
end

_G.DrakeishUI_ApplyChatView = ApplyChatView
_G.DrakeishUI_SetChatView = function(view)
    if view ~= "combat" then view = "chat" end
    MinimalChatDB.view = view
    ApplyChatView()
end
_G.DrakeishUI_ToggleChatView = function()
    if MinimalChatDB.view == "combat" then
        MinimalChatDB.view = "chat"
    else
        MinimalChatDB.view = "combat"
    end
    ApplyChatView()
    return MinimalChatDB.view
end

-- One-shot layout (position + chrome). Safe to call a few times at login.
local function ApplyChatPositioning()
    local chat = ChatFrame1
    local editBox = ChatFrame1EditBox
    if not chat or not editBox then return end

    if addon.Layout then
        local db=addon.Layout:DB()
        local width,height=UIParent:GetWidth(),UIParent:GetHeight()
        if width>0 and height>0 and type(db.bars.MinimalChatAnchor)~="table" then
            local function valid(v) return type(v)=="number" and v==v and math.abs(v)<100000 end
            local x=valid(MinimalChatDB.x) and MinimalChatDB.x or 16
            local y=valid(MinimalChatDB.y) and MinimalChatDB.y or 40
            db.bars.MinimalChatAnchor={x=(x+150)/width,y=(y+50)/height,scale=1}
            MinimalChatDB.x=nil;MinimalChatDB.y=nil
        end
        if addon.Layout.entries.MinimalChatAnchor then addon.Layout:ApplyBar(chatAnchor)
        else addon.Layout:RefreshBars() end
    end

    StyleChatChromeOnce()
    PlaceChatFrame(ChatFrame1)
    PlaceChatFrame(GetCombatLogFrame())

    PlaceAllChatEditBoxes()

    ApplyChatView()
end

-- Bars and chat frames are siblings under UIParent, so an anchor scale does
-- not automatically resize the chat or its edit boxes.
chatAnchor._drakeishLayoutChanged=function()
    PlaceChatFrame(ChatFrame1);PlaceChatFrame(GetCombatLogFrame());PlaceAllChatEditBoxes()
    if _G.DrakeishUI_PositionChatInfo then _G.DrakeishUI_PositionChatInfo() end
end

f:SetScript("OnEvent", ApplyChatPositioning)

local chatToggleButton
local chatViewButton
do
	chatToggleButton = CreateFrame('CheckButton', 'chattoggle', UIParent)
	chatToggleButton:SetSize(10, 14)
	chatToggleButton:SetPoint("LEFT", chatAnchor, "TOPLEFT", 2, 17)
	chatToggleButton:SetAlpha(0.3)
	chatToggleButton:SetFrameStrata('HIGH')
	chatToggleButton:SetFrameLevel(100)
	chatToggleButton:SetNormalTexture''
	chatToggleButton:SetPushedTexture''
	chatToggleButton:SetHighlightTexture''
	chatToggleButton:RegisterForClicks('LeftButtonUp')
	chatToggleButton:EnableMouse(true)

	local normal = chatToggleButton:GetNormalTexture()
	normal:set_atlas('bag-arrow-2x')

	local pushed = chatToggleButton:GetPushedTexture()
	pushed:set_atlas('bag-arrow-2x')

	local highlight = chatToggleButton:GetHighlightTexture()
	highlight:set_atlas('bag-arrow-2x')
	highlight:SetAlpha(.4)
	highlight:SetBlendMode('ADD')

	-- Chat / Combat Log switcher (WotLK gossip icons: bubble + battlemaster swords)
	chatViewButton = CreateFrame('Button', 'DrakeishUI_ChatViewToggle', UIParent)
	chatViewButton:SetSize(18, 18)
	chatViewButton:SetPoint("LEFT", chatToggleButton, "RIGHT", 6, 0)
	chatViewButton:SetAlpha(0.3) -- match arrow / swords idle transparency
	chatViewButton:SetFrameStrata('TOOLTIP')
	chatViewButton:SetFrameLevel(200)
	chatViewButton:EnableMouse(true)
	chatViewButton:RegisterForClicks('AnyUp')

	local viewIcon = chatViewButton:CreateTexture(nil, 'OVERLAY')
	viewIcon:SetPoint('CENTER', chatViewButton, 'CENTER', 0, 0)
	viewIcon:SetSize(18, 18)
	chatViewButton.icon = viewIcon

	-- These paths exist in 3.3.5 (ChatBubbleGossipIcon does not)
	local CHAT_ICON = "Interface\\GossipFrame\\GossipGossipIcon"
	local COMBAT_ICON = "Interface\\GossipFrame\\BattleMasterGossipIcon"

	local function UpdateChatViewButtonVisibility()
		if chat_state == 1 then
			chatViewButton:Hide()
		else
			chatViewButton:Show()
		end
	end

	local function UpdateChatViewButtonIcon()
		-- Icon is the view the click switches TO, not the one on screen.
		if MinimalChatDB.view == "combat" then
			viewIcon:SetTexture(CHAT_ICON)
			-- Bubble: rotate 180°
			viewIcon:SetTexCoord(1, 0, 1, 0)
			viewIcon:SetVertexColor(1, 1, 1, 0.65)
			chatViewButton:SetAlpha(0.22)
		else
			viewIcon:SetTexture(COMBAT_ICON)
			-- Swords: normal orientation
			viewIcon:SetTexCoord(0, 1, 0, 1)
			viewIcon:SetVertexColor(1, 1, 1, 1)
			chatViewButton:SetAlpha(0.3)
		end
		viewIcon:Show()
		UpdateChatViewButtonVisibility()
	end

	chatViewButton:SetScript('OnClick', function(self, button)
		if MinimalChatDB.view == "combat" then
			MinimalChatDB.view = "chat"
		else
			MinimalChatDB.view = "combat"
		end
		UpdateChatViewButtonIcon()
		ApplyChatView()
	end)

	chatViewButton:SetScript("OnEnter", function(self)
		self:SetAlpha(1.0)
		viewIcon:SetVertexColor(1, 1, 1, 1)
		GameTooltip:SetOwner(self, "ANCHOR_TOP")
		if MinimalChatDB.view == "combat" then
			GameTooltip:SetText("Chat")
			GameTooltip:AddLine("Click to switch to Chat.", 1, 1, 1, true)
		else
			GameTooltip:SetText("Combat Log")
			GameTooltip:AddLine("Click to switch to Combat Log.", 1, 1, 1, true)
		end
		GameTooltip:Show()
	end)
	chatViewButton:SetScript("OnLeave", function(self)
		UpdateChatViewButtonIcon()
		GameTooltip:Hide()
	end)

	UpdateChatViewButtonIcon()
	
	chatToggleButton:SetScript('OnClick',function(self)
		local checked = self:GetChecked();
		if checked then
			normal:set_atlas('bag-arrow-invert-2x')
			pushed:set_atlas('bag-arrow-invert-2x')
			highlight:set_atlas('bag-arrow-invert-2x')
			chat_state = 1
			ApplyChatView()
			UpdateChatViewButtonVisibility()
			if ChatFrame1EditBox then ChatFrame1EditBox:Hide() end
			if _G.UnifiedInfoFrame then
				_G.UnifiedInfoFrame:Hide()
			end
		else
			normal:set_atlas('bag-arrow-2x')
			pushed:set_atlas('bag-arrow-2x')
			highlight:set_atlas('bag-arrow-2x')
			chat_state = 0
			ApplyChatView()
			UpdateChatViewButtonVisibility()
			if _G.UnifiedInfoFrame then
				_G.UnifiedInfoFrame:Show()
			end
		end
	end)
	
	chatToggleButton:SetScript("OnEnter", function(self)
		self:SetAlpha(1.0)
	end)
	chatToggleButton:SetScript("OnLeave", function(self)
		self:SetAlpha(0.3)
	end)
	
	local initTimer = CreateFrame("Frame")
	initTimer:SetScript("OnUpdate", function(self, delta)
		self.elapsed = (self.elapsed or 0) + delta
		if self.elapsed >= 0.1 then
			self:SetScript("OnUpdate", nil)
			if not chat_state then chat_state = 0 end
			UpdateChatViewButtonIcon()
			if chat_state == 1 then
				if ChatFrame1EditBox then ChatFrame1EditBox:Hide() end
				if _G.UnifiedInfoFrame then
					_G.UnifiedInfoFrame:Hide()
				end
				normal:set_atlas('bag-arrow-invert-2x')
				pushed:set_atlas('bag-arrow-invert-2x')
				highlight:set_atlas('bag-arrow-invert-2x')
				chatToggleButton:SetChecked(1)
			else
				if _G.UnifiedInfoFrame then
					_G.UnifiedInfoFrame:Show()
				end
				normal:set_atlas('bag-arrow-2x')
				pushed:set_atlas('bag-arrow-2x')
				highlight:set_atlas('bag-arrow-2x')
				chatToggleButton:SetChecked(nil)
			end
			ApplyChatView()
			UpdateChatViewButtonVisibility()
		end
	end)
end

-- Screen-message positions are owned by native_notifications.lua and its
-- per-character settings, rather than an unconditional raise at login.
