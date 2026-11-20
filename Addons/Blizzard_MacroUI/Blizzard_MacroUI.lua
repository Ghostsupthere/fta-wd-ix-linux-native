MAX_ACCOUNT_MACROS = 36;
MAX_CHARACTER_MACROS = 18;
NUM_MACROS_PER_ROW = 6;
NUM_MACRO_ICONS_SHOWN = 20;
NUM_ICONS_PER_ROW = 5;
NUM_ICON_ROWS = 4;
MACRO_ICON_ROW_HEIGHT = 36;

local extendedMacros = type(GetMacroBodyLimits) == "function" and type(GetMacroBodyInfo) == "function";
local macroBodyLimit = extendedMacros and GetMacroBodyLimits() or 255;

local function MacroFrame_ShowError(message)
    UIErrorsFrame:AddMessage(message, 1, .15, .15, 1);
end

function MacroFrame_RefreshCounter(editBox)
    if extendedMacros then
        local characters, bytes, problem = GetMacroBodyInfo(editBox:GetText());
        if characters then
            MacroFrameCharLimitText:SetFormattedText("%d/%d characters (%d bytes)", characters, macroBodyLimit, bytes);
        else
            MacroFrameCharLimitText:SetText("Invalid UTF-8 macro text");
        end
        if problem then
            MacroFrameCharLimitText:SetTextColor(1, .25, .25);
        else
            MacroFrameCharLimitText:SetTextColor(1, 1, 1);
        end
    else
        MacroFrameCharLimitText:SetFormattedText(MACROFRAME_CHAR_LIMIT, editBox:GetNumLetters());
    end
end

function MacroFrame_OnBodyChanged(editBox)
    if not MacroFrame.settingBody then
        MacroFrame.textChanged = not extendedMacros or editBox:GetText() ~= MacroFrame.savedBody;
    end
    if MacroPopupFrame.mode == "new" then
        MacroPopupFrame:Hide();
    end
    MacroFrame_RefreshCounter(editBox);
    ScrollingEdit_OnTextChanged(editBox, editBox:GetParent());
    if MacroEditor_BodyChanged then MacroEditor_BodyChanged(); end
end

function MacroFrame_OnBodySet(editBox)
    if extendedMacros and not MacroFrame.settingBody then
        MacroFrame.textChanged = editBox:GetText() ~= MacroFrame.savedBody;
        MacroFrame_RefreshCounter(editBox);
    end
    if MacroEditor_BodyChanged then MacroEditor_BodyChanged(); end
end

UIPanelWindows["MacroFrame"] = { area = "left", pushable = 5, whileDead = 1 };

function MacroFrame_Show()
	ShowUIPanel(MacroFrame);
end

function MacroFrame_OnLoad(self)
    if extendedMacros then
        -- Keep a pasted draft intact so the editor can explain a rejected save.
        MacroFrameText:SetMaxLetters(0);
        MacroFrameText:SetMaxBytes(0);
        MacroSaveButton:Show();
    end
    MacroFrame_RefreshCounter(MacroFrameText);
	MacroFrame_SetAccountMacros();
	PanelTemplates_SetNumTabs(MacroFrame, 2);
	PanelTemplates_SetTab(MacroFrame, 1);
    if MacroEditor_Initialize then MacroEditor_Initialize(); end
end

function MacroFrame_OnShow(self)
	MacroFrame_Update();
	PlaySound("igCharacterInfoOpen");
	UpdateMicroButtons();
end

function MacroFrame_OnHide(self)
	MacroPopupFrame:Hide();
	MacroFrame_SaveMacro();
	--SaveMacros();
	PlaySound("igCharacterInfoClose");
	UpdateMicroButtons();
end

function MacroFrame_SetAccountMacros()
	MacroFrame.macroBase = 0;
	MacroFrame.macroMax = MAX_ACCOUNT_MACROS;
	local numAccountMacros, numCharacterMacros = GetNumMacros();
	if ( numAccountMacros > 0 ) then
		MacroFrame_SelectMacro(MacroFrame.macroBase + 1);
	else
		MacroFrame_SelectMacro(nil);
	end
end

function MacroFrame_SetCharacterMacros()
	MacroFrame.macroBase = MAX_ACCOUNT_MACROS;
	MacroFrame.macroMax = MAX_CHARACTER_MACROS;
	local numAccountMacros, numCharacterMacros = GetNumMacros();
	if ( numCharacterMacros > 0 ) then
		MacroFrame_SelectMacro(MacroFrame.macroBase + 1);
	else
		MacroFrame_SelectMacro(nil);
	end
end

function MacroFrame_Update()
	local numMacros;
	local numAccountMacros, numCharacterMacros = GetNumMacros();
	local macroButtonName, macroButton, macroIcon, macroName;
	local name, texture, body;
	local selectedName, selectedBody, selectedIcon;

	if ( MacroFrame.macroBase == 0 ) then
		numMacros = numAccountMacros;
	else
		numMacros = numCharacterMacros;
	end

	-- Macro List
	local maxMacroButtons = max(MAX_ACCOUNT_MACROS, MAX_CHARACTER_MACROS);
	for i=1, maxMacroButtons do
		macroButtonName = "MacroButton"..i;
		macroButton = _G[macroButtonName];
		macroIcon = _G[macroButtonName.."Icon"];
		macroName = _G[macroButtonName.."Name"];
		if ( i <= MacroFrame.macroMax ) then
			if ( i <= numMacros ) then
				name, texture, body = GetMacroInfo(MacroFrame.macroBase + i);
				macroIcon:SetTexture(texture);
				macroName:SetText(name);
				macroButton:Enable();
				-- Highlight Selected Macro
				if ( MacroFrame.selectedMacro and (i == (MacroFrame.selectedMacro - MacroFrame.macroBase)) ) then
					macroButton:SetChecked(1);
					MacroFrameSelectedMacroName:SetText(name);
                    if not MacroFrame.textChanged then
                        MacroFrame.savedBody = body or "";
                        MacroFrame.settingBody = true;
                        MacroFrameText:SetText(MacroFrame.savedBody);
                        MacroFrame.settingBody = nil;
                        MacroFrame_RefreshCounter(MacroFrameText);
                    end
					MacroFrameSelectedMacroButton:SetID(i);
					MacroFrameSelectedMacroButtonIcon:SetTexture(texture);
					MacroPopupFrame.selectedIconTexture = texture;
				else
					macroButton:SetChecked(0);
				end
			else
				macroButton:SetChecked(0);
				macroIcon:SetTexture("");
				macroName:SetText("");
				macroButton:Disable();
			end
			macroButton:Show();
		else
			macroButton:Hide();
		end
	end

	-- Macro Details
	if ( MacroFrame.selectedMacro ~= nil ) then
		MacroFrame_ShowDetails();
		MacroDeleteButton:Enable();
	else
		MacroFrame_HideDetails();
		MacroDeleteButton:Disable();
	end
	
	--Update New Button
	if ( numMacros < MacroFrame.macroMax ) then
		MacroNewButton:Enable();
	else
		MacroNewButton:Disable();
	end

	-- Disable Buttons
	if ( MacroPopupFrame:IsShown() ) then
		MacroEditButton:Disable();
		MacroDeleteButton:Disable();
	else
		MacroEditButton:Enable();
		MacroDeleteButton:Enable();
	end

	if ( not MacroFrame.selectedMacro ) then
		MacroDeleteButton:Disable();
	end
end

function MacroFrame_AddMacroLine(line)
	if ( MacroFrameText:IsVisible() ) then
		MacroFrameText:SetText(MacroFrameText:GetText()..line);
	end
end

function MacroButton_OnClick(self, button)
	if not MacroFrame_SaveMacro() then return end
	MacroFrame_SelectMacro(MacroFrame.macroBase + self:GetID());
	MacroFrame_Update();
	MacroPopupFrame:Hide();
	MacroFrameText:ClearFocus();
end

function MacroFrame_SelectMacro(id)
	MacroFrame.selectedMacro = id;
end

function MacroFrame_DeleteMacro()
	local selectedMacro = MacroFrame.selectedMacro;
    DeleteMacro(selectedMacro);
    MacroFrame.textChanged = nil;
	-- the order of the return values (account macros, character macros) matches up with the IDs of the tabs
	local numMacros = select(PanelTemplates_GetSelectedTab(MacroFrame), GetNumMacros());
	if ( selectedMacro > numMacros + MacroFrame.macroBase) then
		selectedMacro = selectedMacro - 1;
	end
	if ( selectedMacro <= MacroFrame.macroBase ) then
		MacroFrame.selectedMacro = nil;
	else
		MacroFrame.selectedMacro = selectedMacro;
	end
	MacroFrame_Update();
	MacroFrameText:ClearFocus();
end

function MacroNewButton_OnClick(self, button)
	if not MacroFrame_SaveMacro() then return end
	MacroPopupFrame.mode = "new";
	MacroPopupFrame:Show();
end

function MacroEditButton_OnClick(self, button)
	if not MacroFrame_SaveMacro() then return end
	MacroPopupFrame.mode = "edit";
	MacroPopupFrame:Show();
end

function MacroFrame_HideDetails()
	MacroEditButton:Hide();
	MacroFrameCharLimitText:Hide();
	MacroFrameText:Hide();
	MacroFrameSelectedMacroName:Hide();
	MacroFrameSelectedMacroBackground:Hide();
	MacroFrameSelectedMacroButton:Hide();
end

function MacroFrame_ShowDetails()
	MacroEditButton:Show();
	MacroFrameCharLimitText:Show();
	MacroFrameEnterMacroText:Show();
	MacroFrameText:Show();
	MacroFrameSelectedMacroName:Show();
	MacroFrameSelectedMacroBackground:Show();
	MacroFrameSelectedMacroButton:Show();
end

function MacroButtonContainer_OnLoad(self)
	local button;
	local maxMacroButtons = max(MAX_ACCOUNT_MACROS, MAX_CHARACTER_MACROS);
	for i=1, maxMacroButtons do
		button = CreateFrame("CheckButton", "MacroButton"..i, self, "MacroButtonTemplate");
		button:SetID(i);
		if ( i == 1 ) then
			button:SetPoint("TOPLEFT", self, "TOPLEFT", 6, -6);
		elseif ( mod(i, NUM_MACROS_PER_ROW) == 1 ) then
			button:SetPoint("TOP", "MacroButton"..(i-NUM_MACROS_PER_ROW), "BOTTOM", 0, -10);
		else
			button:SetPoint("LEFT", "MacroButton"..(i-1), "RIGHT", 13, 0);
		end
	end
end

function MacroPopupFrame_OnShow(self)
	MacroPopupEditBox:SetFocus();

	PlaySound("igCharacterInfoOpen");
	MacroPopupFrame_Update(self);
	MacroPopupOkayButton_Update();

	if ( self.mode == "new" ) then
		MacroFrameText:Hide();
		MacroPopupButton_SelectTexture(1);
	end
	
	-- Disable Buttons
	MacroEditButton:Disable();
	MacroDeleteButton:Disable();
	MacroNewButton:Disable();
	MacroFrameTab1:Disable();
	MacroFrameTab2:Disable();
end

function MacroPopupFrame_OnHide(self)
	if ( self.mode == "new" ) then
		MacroFrameText:Show();
		MacroFrameText:SetFocus();
	end
	
	-- Enable Buttons
	MacroEditButton:Enable();
	MacroDeleteButton:Enable();
	local numMacros;
	local numAccountMacros, numCharacterMacros = GetNumMacros();
	if ( MacroFrame.macroBase == 0 ) then
		numMacros = numAccountMacros;
	else
		numMacros = numCharacterMacros;
	end
	if ( numMacros < MacroFrame.macroMax ) then
		MacroNewButton:Enable();
	end
	-- Enable tabs
	PanelTemplates_UpdateTabs(MacroFrame);
end

function MacroPopupFrame_Update(self)
	self = self or MacroPopupFrame;
	local numMacroIcons = GetNumMacroIcons();
	local macroPopupIcon, macroPopupButton;
	local macroPopupOffset = FauxScrollFrame_GetOffset(MacroPopupScrollFrame);
	local index;
	
	-- Determine whether we're creating a new macro or editing an existing one
	if ( self.mode == "new" ) then
		MacroPopupEditBox:SetText("");
	elseif ( self.mode == "edit" ) then
		local name, texture, body = GetMacroInfo(MacroFrame.selectedMacro);
		MacroPopupEditBox:SetText(name);
	end
	
	-- Icon list
	local texture;
	for i=1, NUM_MACRO_ICONS_SHOWN do
		macroPopupIcon = _G["MacroPopupButton"..i.."Icon"];
		macroPopupButton = _G["MacroPopupButton"..i];
		index = (macroPopupOffset * NUM_ICONS_PER_ROW) + i;
		texture = GetMacroIconInfo(index);
		if ( index <= numMacroIcons ) then
			macroPopupIcon:SetTexture(texture);
			macroPopupButton:Show();
		else
			macroPopupIcon:SetTexture("");
			macroPopupButton:Hide();
		end
		if ( MacroPopupFrame.selectedIcon and (index == MacroPopupFrame.selectedIcon) ) then
			macroPopupButton:SetChecked(1);
		elseif ( MacroPopupFrame.selectedIconTexture ==  texture ) then
			macroPopupButton:SetChecked(1);
		else
			macroPopupButton:SetChecked(nil);
		end
	end
	
	-- Scrollbar stuff
	FauxScrollFrame_Update(MacroPopupScrollFrame, ceil(numMacroIcons / NUM_ICONS_PER_ROW) , NUM_ICON_ROWS, MACRO_ICON_ROW_HEIGHT );
end

function MacroPopupFrame_CancelEdit()
	MacroPopupFrame:Hide();
	MacroFrame_Update();
	MacroPopupFrame.selectedIcon = nil;
end

function MacroPopupOkayButton_Update()
	if ( (strlen(MacroPopupEditBox:GetText()) > 0) and MacroPopupFrame.selectedIcon ) then
		MacroPopupOkayButton:Enable();
	else
		MacroPopupOkayButton:Disable();
	end
	if ( MacroPopupFrame.mode == "edit" and (strlen(MacroPopupEditBox:GetText()) > 0) ) then
		MacroPopupOkayButton:Enable();
	end
end

function MacroPopupButton_SelectTexture(selectedIcon)
	MacroPopupFrame.selectedIcon = selectedIcon;
	-- Clear out selected texture
	MacroPopupFrame.selectedIconTexture = nil;
	MacroFrameSelectedMacroButtonIcon:SetTexture(GetMacroIconInfo(MacroPopupFrame.selectedIcon));
	MacroPopupOkayButton_Update();
	local mode = MacroPopupFrame.mode;
	MacroPopupFrame.mode = nil;
	MacroPopupFrame_Update(MacroPopupFrame);
	MacroPopupFrame.mode = mode;
end

function MacroPopupButton_OnClick(self, button)
	MacroPopupButton_SelectTexture(self:GetID() + (FauxScrollFrame_GetOffset(MacroPopupScrollFrame) * NUM_ICONS_PER_ROW));
end

function MacroPopupOkayButton_OnClick(self, button)
	local index = 1
	if ( MacroPopupFrame.mode == "new" ) then
		index = CreateMacro(MacroPopupEditBox:GetText(), MacroPopupFrame.selectedIcon, nil, (MacroFrame.macroBase > 0));
	elseif ( MacroPopupFrame.mode == "edit" ) then
		index = EditMacro(MacroFrame.selectedMacro, MacroPopupEditBox:GetText(), MacroPopupFrame.selectedIcon);
	end
	MacroPopupFrame:Hide();
	MacroFrame_SelectMacro(index);
	MacroFrame_Update();
end

function MacroFrame_SaveMacro()
    if not MacroFrame.selectedMacro then
        return true;
    end

    if not extendedMacros then
        if MacroFrame.textChanged then
            EditMacro(MacroFrame.selectedMacro, nil, nil, MacroFrameText:GetText());
            MacroFrame.textChanged = nil;
        end
        return true;
    end
    local body = MacroFrameText:GetText();
    MacroFrame.textChanged = body ~= MacroFrame.savedBody;
    if not MacroFrame.textChanged then
        return true;
    end
    local characters, _, problem = GetMacroBodyInfo(body);
    if problem then
        if characters and characters > macroBodyLimit then
            problem = format("Macros can contain up to %d characters. This draft has %d.", macroBodyLimit, characters);
        end
        MacroFrame_ShowError(problem);
        return false;
    end
    local ok, index = pcall(EditMacro, MacroFrame.selectedMacro, nil, nil, body);
    if not ok or not index or index < 1 then
        local failure = not ok and tostring(index);
        MacroFrame_ShowError(failure and (failure:match("^.-:%d+: (.*)$") or failure)
            or "The macro could not be saved right now. Your draft was kept.");
        return false;
    end
    MacroFrame.selectedMacro = index;
    MacroFrame.savedBody = body;
    MacroFrame.textChanged = nil;
    return true;
end
