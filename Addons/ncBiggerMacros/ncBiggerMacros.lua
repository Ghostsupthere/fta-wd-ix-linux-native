local function setbody(index, body)
	if InCombatLockdown() then return end

	local button = _G["ExtraMacros"..index] or CreateFrame("Button", "ExtraMacros"..index, nil, "SecureActionButtonTemplate")
	button:SetAttribute("type", "macro")
	button:SetAttribute("macrotext", body)
end

local function new()
	if InCombatLockdown() then return end

	local index = 1

	if MacroPopupFrame.mode == "new" then
		index = CreateMacro(MacroPopupEditBox:GetText(), MacroPopupFrame.selectedIcon, nil, MacroFrame.macroBase > 0)
		local global, perchar = GetNumMacros()
		if MacroFrame.macroBase == 0 then
			for i = global - 1, index, -1 do
				StoredMacros[i + 1] = StoredMacros[i]
			end
			StoredMacros[index] = nil
		else
			for i = perchar + 36 - 1, index, -1 do
				StoredMacrosPerCharacter[i + 1] = StoredMacrosPerCharacter[i]
			end
			StoredMacrosPerCharacter[index] = nil
		end
	elseif MacroPopupFrame.mode == "edit" then
		index = EditMacro(MacroFrame.selectedMacro, MacroPopupEditBox:GetText(), MacroPopupFrame.selectedIcon)
	end
	MacroPopupFrame:Hide()
	MacroFrame_SelectMacro(index)
	MacroFrame_Update()
end

local function save()
	if InCombatLockdown() then return end

	if MacroFrame.textChanged and MacroFrame.selectedMacro then
		local body = MacroFrameText:GetText()

		if body:len() < 256 then
			if MacroFrame.macroBase == 0 then
				StoredMacros[MacroFrame.selectedMacro] = nil
			else
				StoredMacrosPerCharacter[MacroFrame.selectedMacro] = nil
			end

			EditMacro(MacroFrame.selectedMacro, nil, nil, body)
		else
			if MacroFrame.macroBase == 0 then
				StoredMacros[MacroFrame.selectedMacro] = body
			else
				StoredMacrosPerCharacter[MacroFrame.selectedMacro] = body
			end

			EditMacro(MacroFrame.selectedMacro, nil, nil, "/click ExtraMacros"..MacroFrame.selectedMacro)
			setbody(MacroFrame.selectedMacro, body)
		 end

		MacroFrame.textChanged = nil
	end
end

local function delete()
	if InCombatLockdown() then return end

	local selectedMacro = MacroFrame.selectedMacro
	local global, perchar = GetNumMacros()

	DeleteMacro(selectedMacro)
	
	if MacroFrame.macroBase == 0 then
		for i = selectedMacro, 35 do
			StoredMacros[i] = StoredMacros[i + 1]
			if StoredMacros[i] then
				EditMacro(i, nil, nil, "/click ExtraMacros"..i)
			end
		end
		StoredMacros[global] = nil
	else
		for i = selectedMacro, 71 do
			StoredMacrosPerCharacter[i] = StoredMacrosPerCharacter[i + 1]
			if StoredMacrosPerCharacter[i] then
				EditMacro(i, nil, nil, "/click ExtraMacros"..i)
			end
		end
		StoredMacrosPerCharacter[perchar] = nil
	end

	-- the order of the return values (account macros, character macros) matches up with the IDs of the tabs
	local numMacros = select(PanelTemplates_GetSelectedTab(MacroFrame), GetNumMacros())
	if selectedMacro > numMacros + MacroFrame.macroBase then
		selectedMacro = selectedMacro - 1
	end

	if selectedMacro <= MacroFrame.macroBase then
		MacroFrame.selectedMacro = nil
	else
		MacroFrame.selectedMacro = selectedMacro
	end
	MacroFrame_Update()
	MacroFrameText:ClearFocus()
end

local function update()
	local numMacros
	local numAccountMacros, numCharacterMacros = GetNumMacros()
	local macroButtonName, macroButton, macroIcon, macroName
	local name, texture, body
	local selectedName, selectedBody, selectedIcon
 
	if MacroFrame.macroBase == 0 then
		numMacros = numAccountMacros
	else
		numMacros = numCharacterMacros
	end
 
	-- Macro List
	local maxMacroButtons = max(MAX_ACCOUNT_MACROS, MAX_CHARACTER_MACROS)
	for i=1, maxMacroButtons do
		macroButtonName = "MacroButton"..i
		macroButton = _G[macroButtonName]
		macroIcon = _G[macroButtonName.."Icon"]
		macroName = _G[macroButtonName.."Name"]
		if i <= MacroFrame.macroMax then
			if i <= numMacros then
				name, texture, body = GetMacroInfo(MacroFrame.macroBase + i)
				body = MacroFrame.macroBase==0 and StoredMacros[i] or MacroFrame.macroBase==36 and StoredMacrosPerCharacter[36 + i] or body -- load macro from database
				macroIcon:SetTexture(texture)
				macroName:SetText(name)
				macroButton:Enable()
				-- Highlight Selected Macro
				if MacroFrame.selectedMacro and i == (MacroFrame.selectedMacro - MacroFrame.macroBase) then
					macroButton:SetChecked(1)
					MacroFrameSelectedMacroName:SetText(name)
					MacroFrameText:SetText(body)
					MacroFrameSelectedMacroButton:SetID(i)
					MacroFrameSelectedMacroButtonIcon:SetTexture(texture)
					MacroPopupFrame.selectedIconTexture = texture
				else
					macroButton:SetChecked(0)
				end
			else
				macroButton:SetChecked(0)
				macroIcon:SetTexture("")
				macroName:SetText("")
				macroButton:Disable()
			end
			macroButton:Show()
		else
			macroButton:Hide()
		end
	end
 
	-- Macro Details
	if MacroFrame.selectedMacro ~= nil then
		MacroFrame_ShowDetails()
		MacroDeleteButton:Enable()
	else
		MacroFrame_HideDetails()
		MacroDeleteButton:Disable()
	end
	 
	--Update New Button
	if numMacros < MacroFrame.macroMax then
		MacroNewButton:Enable()
	else
		MacroNewButton:Disable()
	end
 
	-- Disable Buttons
	if MacroPopupFrame:IsShown() then
		MacroEditButton:Disable()
		MacroDeleteButton:Disable()
	else
		MacroEditButton:Enable()
		MacroDeleteButton:Enable()
	end
 
	if not MacroFrame.selectedMacro then
		MacroDeleteButton:Disable()
	end
end

-- The client has native long macros (2048 characters) when these exist. Then this add-on must not replace the macro window
-- functions (its replacements only know the old 255-character client and break selecting a macro). It only migrates what it
-- stored earlier: a macro whose text is exactly its "/click ExtraMacrosN" stub gets its full text back.
local native = type(GetMacroBodyLimits) == "function" and type(GetMacroBodyInfo) == "function"

local function migrate()
	local moved = 0
	for _, store in ipairs({StoredMacros or {}, StoredMacrosPerCharacter or {}}) do
		for index, body in pairs(store) do
			if type(index) == "number" and type(body) == "string" and body ~= "" then
				local name, _, current = GetMacroInfo(index)
				if name and current and current:gsub("%s+$", "") == "/click ExtraMacros" .. index then
					local ok = pcall(EditMacro, index, nil, nil, body)
					if ok then moved = moved + 1 end
				end
			end
		end
	end
	if moved > 0 then
		DEFAULT_CHAT_FRAME:AddMessage("|cff33ccffncBiggerMacros:|r moved " .. moved .. " long macro(s) into the client's own macro storage.")
	end
end

local holder = CreateFrame("Frame")
holder:RegisterEvent("ADDON_LOADED")
holder:SetScript("OnEvent", function(f, event, addon)
	if native and addon == "Blizzard_MacroUI" then
		return
	end
	if addon == "ncBiggerMacros" then
		if native then
			-- macros load a moment after login: try for up to 15 seconds
			local waited, done = 0, false
			f:SetScript("OnUpdate", function(self, elapsed)
				waited = waited + elapsed
				local a, c = GetNumMacros()
				if (a + c > 0 and waited > 2) or waited > 15 then
					self:SetScript("OnUpdate", nil)
					if a + c > 0 then migrate() end
				end
			end)
		end
		StoredMacros = StoredMacros or {}
		StoredMacrosPerCharacter = StoredMacrosPerCharacter or {}

		for index, body in pairs(StoredMacros) do
			setbody(index, body)
		end

		for index, body in pairs(StoredMacrosPerCharacter) do
			setbody(index, body)
		end
	elseif addon=="Blizzard_MacroUI" then
		MacroFrameText:SetMaxLetters(1024)
		MACROFRAME_CHAR_LIMIT = MACROFRAME_CHAR_LIMIT:gsub("%d+", "1024")

		MacroFrame_SaveMacro = save
		MacroFrame_DeleteMacro = delete
		MacroFrame_Update = update
		MacroPopupOkayButton_OnClick = new
	end
end)
