local api = C_BonusLoot
local frame = CreateFrame("Frame", "FTABonusLootFrame", UIParent)
local name = frame:GetName()
local gold = {1, .78, .36}
local sounds = {
    offer = "Sound\\Interface\\bonusloot.wav",
    rolling = "Sound\\Interface\\AuctionWindowOpen.wav",
    gold = "Sound\\Interface\\LootCoinSmall.wav",
    item = "Sound\\Spells\\AchievmentSound1.wav",
}
local revealDelay, rewardDuration = .62, 7

frame:SetSize(388, 166)
frame:SetPoint("TOP", UIParent, "TOP", 0, -132)
frame:SetFrameStrata("DIALOG")
frame:SetClampedToScreen(true)
frame:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background-Dark",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Gold-Border",
    tile = true, tileSize = 32, edgeSize = 24,
    insets = {left = 5, right = 5, top = 5, bottom = 5},
})
frame:SetBackdropColor(.28, .20, .15, .995)
frame:Hide()

local warmth = frame:CreateTexture(nil, "BACKGROUND")
warmth:SetPoint("TOPLEFT", 8, -8)
warmth:SetPoint("TOPRIGHT", -8, -8)
warmth:SetHeight(104)
warmth:SetTexture(1, 1, 1, 1)
warmth:SetGradientAlpha("VERTICAL", .22, .08, .024, 0, .24, .10, .028, .56)

local plaque = frame:CreateTexture(nil, "BORDER")
plaque:SetTexture("Interface\\AchievementFrame\\UI-Achievement-Alert-Background")
plaque:SetTexCoord(5/512, 309/512, 2/128, 88/128)
plaque:SetPoint("TOPLEFT", 95, -8)
plaque:SetPoint("TOPRIGHT", -10, -8)
plaque:SetHeight(67)
plaque:SetVertexColor(.68, .60, .49)

local sweep = frame:CreateTexture(nil, "BORDER")
sweep:SetTexture("Interface\\Cooldown\\star4")
sweep:SetBlendMode("ADD")
sweep:SetSize(132, 24)
sweep:SetAlpha(0)

local divider = frame:CreateTexture(nil, "BORDER")
divider:SetPoint("BOTTOMLEFT", 18, 53)
divider:SetPoint("BOTTOMRIGHT", -18, 53)
divider:SetHeight(1)
divider:SetTexture(.66, .44, .20, .5)

local function text(role, font, x, y, width, height)
    local label = frame:CreateFontString(name .. role, "ARTWORK", font)
    label:SetPoint("TOPLEFT", x, y)
    label:SetSize(width, height)
    label:SetJustifyH("LEFT")
    label:SetJustifyV("TOP")
    return label
end

local title = text("Title", "GameFontNormalLarge", 126, -23, 224, 21)
title:SetTextColor(1, .88, .62)
local subject = text("Subject", "GameFontHighlight", 126, -46, 229, 27)
local detail = text("Detail", "GameFontHighlightSmall", 105, -83, 259, 26)
detail:SetTextColor(.89, .82, .72)

local well = CreateFrame("Frame", nil, frame)
well:SetSize(72, 72)
well:SetPoint("TOPLEFT", 19, -28)
well:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background-Dark",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Gold-Border",
    edgeSize = 13,
    insets = {left = 3, right = 3, top = 3, bottom = 3},
})
well:SetBackdropColor(.42, .31, .21, 1)

local bezel = well:CreateTexture(nil, "OVERLAY")
bezel:SetTexture("Interface\\AchievementFrame\\UI-Achievement-IconFrame")
bezel:SetTexCoord(6/128, 66/128, 6/128, 66/128)
bezel:SetAllPoints()

local icon = CreateFrame("Button", name .. "Item", well)
icon:SetSize(53, 53)
icon:SetPoint("CENTER")

local artwork = icon:CreateTexture(nil, "ARTWORK")
artwork:SetAllPoints()
artwork:SetTexCoord(.055, .945, .055, .945)

local count = icon:CreateFontString(name .. "Count", "OVERLAY", "NumberFontNormalSmall")
count:SetPoint("BOTTOMRIGHT", -2, 2)

local function shine(role, member, size)
    local texture = icon:CreateTexture(name .. role, "OVERLAY")
    texture:SetTexture(member)
    texture:SetBlendMode("ADD")
    texture:SetSize(size, size)
    texture:SetPoint("CENTER")
    return texture
end

local halo = shine("Halo", "Interface\\Buttons\\UI-ActionButton-Border", 89)
local spark = shine("Spark", "Interface\\Cooldown\\star4", 32)

icon:SetScript("OnEnter", function(self)
    if self.link then
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetHyperlink(self.link)
        GameTooltip:Show()
    end
end)
icon:SetScript("OnLeave", function(self)
    if GameTooltip:IsOwned(self) then GameTooltip:Hide() end
end)

local status = frame:CreateFontString(name .. "Status", "ARTWORK", "GameFontHighlightSmall")
status:SetPoint("BOTTOMLEFT", 21, 34)
status:SetWidth(169)
status:SetJustifyH("LEFT")

local progress = CreateFrame("StatusBar", name .. "Progress", frame)
progress:SetSize(169, 6)
progress:SetPoint("BOTTOMLEFT", 21, 21)
progress:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
progress:SetMinMaxValues(0, 1)
local track = progress:CreateTexture(nil, "BACKGROUND")
track:SetAllPoints()
track:SetTexture(.32, .21, .13, .85)

local function button(role, offset, width)
    local control = CreateFrame("Button", name .. role, frame, "UIPanelButtonTemplate")
    control:SetSize(width, 25)
    control:SetPoint("BOTTOMRIGHT", offset, 18)
    control:SetText(role)
    return control
end

local roll = button("Roll", -18, 90)
local pass = button("Pass", -115, 69)
for _, texture in ipairs({pass:GetNormalTexture(), pass:GetPushedTexture()}) do
    texture:SetDesaturated(true)
    texture:SetVertexColor(.70, .64, .55)
end
local close = CreateFrame("Button", name .. "Close", frame, "UIPanelCloseButton")
close:SetSize(25, 25)
close:SetPoint("TOPRIGHT", -5, -5)

local phase, phaseAt, flash
local currentOffer, offerDuration, action, rollAt
local lastResult, reward, revealAt, rewardUntil
local errors = {
    charm = "You need a charm.",
    unavailable = "You can roll after combat.",
}

local function setVisible(region, visible)
    if visible then region:Show() else region:Hide() end
end

local function play(cue)
    if PlaySoundFile then PlaySoundFile(sounds[cue]) end
end

local function setAccent(r, g, b)
    well:SetBackdropBorderColor(r, g, b)
    halo:SetVertexColor(r, g, b)
    spark:SetVertexColor(r, g, b)
    sweep:SetVertexColor(r, g, b)
    bezel:SetVertexColor(.55 + .45 * r, .55 + .45 * g, .55 + .45 * b)
end

local function setItem(texture, link, quantity)
    if icon.link ~= link and GameTooltip:IsOwned(icon) then GameTooltip:Hide() end
    icon.link = link
    artwork:SetTexture(texture or "Interface\\Icons\\INV_Misc_QuestionMark")
    if quantity and quantity > 1 then count:SetText(quantity) else count:SetText("") end
end

local function animate(now)
    if not flash then return end
    local age = now - phaseAt
    local angle = age * -8
    local glow = .67 + .19 * math.sin(age * 8)
    local opacity = .74
    if phase ~= "rolling" then
        local remaining = math.max(0, 1 - age / .85)
        glow = .19 + remaining * (phase == "reward" and .75 or .36)
        opacity = remaining
        if phase == "reward" then
            local travel = 1 - remaining
            sweep:SetPoint("CENTER", plaque, "LEFT", 29 + 218 * travel, 1)
            sweep:SetAlpha(.45 * math.sin(math.pi * travel))
        end
        if remaining == 0 then flash = false end
    end
    halo:SetAlpha(glow)
    halo:SetSize(84 + 13 * opacity, 84 + 13 * opacity)
    spark:SetAlpha(opacity)
    spark:SetPoint("CENTER", icon, "CENTER", 32 * math.cos(angle), 32 * math.sin(angle))
end

local function enterPhase(nextPhase, now, pulse)
    if phase == nextPhase then return false end
    phase, phaseAt, flash = nextPhase, now, pulse
    local showingReward = phase == "reward"
    frame:SetHeight(showingReward and 128 or 166)
    plaque:SetHeight(showingReward and 49 or 67)
    subject:ClearAllPoints()
    subject:SetPoint("TOPLEFT", showingReward and 105 or 126, showingReward and -63 or -46)
    subject:SetWidth(showingReward and 263 or 229)
    detail:ClearAllPoints()
    detail:SetPoint("TOPLEFT", 105, showingReward and -94 or -83)
    setVisible(divider, not showingReward)
    setVisible(status, not showingReward)
    setVisible(progress, not showingReward)
    setVisible(roll, not showingReward)
    setVisible(pass, not showingReward)
    setVisible(close, showingReward)
    spark:SetAlpha(0)
    sweep:SetAlpha(0)
    halo:SetAlpha(.19)
    animate(now)
    return true
end

local function showOffer(offer, now, force)
    local newOffer = currentOffer ~= offer.offerID
    if newOffer then
        currentOffer, offerDuration = offer.offerID, offer.secondsRemaining
        action, rollAt = nil, nil
        reward, revealAt, rewardUntil = nil, nil, nil
        phase = nil
        play("offer")
    elseif not offer.pending then
        action = nil
    end
    local nextPhase = offer.pending and (action == "roll" and "rolling" or "waiting") or "offer"
    local changed = enterPhase(nextPhase, now, newOffer or nextPhase == "rolling")
    local charmCount = GetItemCount(offer.charmItemID)
    local combat = InCombatLockdown()
    if changed or force then
        local charm = GetItemInfo(offer.charmItemID) or "Elder Charm of Good Fortune"
        title:SetText("Bonus Loot Roll")
        subject:SetTextColor(1, .96, .85)
        subject:SetText(offer.bossName)
        detail:SetText(nextPhase == "offer" and ("Spend 1 " .. charm .. " for a chance at extra loot.") or "")
        setAccent(unpack(gold))
        setItem(GetItemIcon(offer.charmItemID), "item:" .. offer.charmItemID, charmCount)
    end

    offerDuration = math.max(offerDuration, offer.secondsRemaining)
    local ratio = offer.secondsRemaining / offerDuration
    progress:SetValue(ratio)
    if ratio <= .2 then
        progress:SetStatusBarColor(.98, .42, .22)
    else
        progress:SetStatusBarColor(.93, .65, .25)
    end
    if offer.pending then
        status:SetText(action == "roll" and "Rolling..." or action == "pass" and "Passing..." or "Waiting for the realm...")
    elseif offer.error then
        status:SetText(errors[offer.error] or "Try again.")
    elseif charmCount == 0 then
        status:SetText("You need a charm.")
    elseif combat then
        status:SetText("You can roll after combat.")
    else
        status:SetFormattedText("%d sec remaining", math.ceil(offer.secondsRemaining))
    end
    if not offer.pending and not combat and charmCount > 0 then roll:Enable() else roll:Disable() end
    if offer.pending then pass:Disable() else pass:Enable() end
    frame:Show()
end

local function showReward(now, force)
    local firstReveal = enterPhase("reward", now, true)
    currentOffer, action, rollAt = nil, nil, nil
    if firstReveal or force then
        title:SetText("Bonus Roll Reward")
        if reward.kind == "gold" then
            setAccent(unpack(gold))
            setItem("Interface\\Icons\\INV_Misc_Coin_01")
            subject:SetTextColor(unpack(gold))
            subject:SetText(GetCoinTextureString(reward.copper))
            detail:SetText("")
        else
            local link = ("item:%d:0:0:0:0:0:%d:0"):format(reward.itemID, reward.randomProperty)
            local itemName, _, quality = GetItemInfo(link)
            local color = ITEM_QUALITY_COLORS[quality]
            if color then
                setAccent(color.r, color.g, color.b)
                subject:SetTextColor(color.r, color.g, color.b)
            else
                setAccent(unpack(gold))
                subject:SetTextColor(1, .96, .85)
            end
            setItem(GetItemIcon(reward.itemID), link, reward.count)
            subject:SetText(itemName or "Bonus loot")
            detail:SetText(reward.kind == "mail" and "Sent to your mailbox." or "Added to your bags.")
        end
    end
    if firstReveal then play(reward.kind == "gold" and "gold" or "item") end
    frame:Show()
end

local function draw(now, force)
    local offer, result = api.GetOffer(), api.GetLastResult()
    if result and result.offerID ~= lastResult then
        lastResult = result.offerID
        if not offer then
            reward = result
            revealAt = (rollAt and currentOffer == result.offerID) and math.max(now, rollAt + revealDelay) or now
            rewardUntil = revealAt + rewardDuration
            if phase == "reward" then phase = nil end
        end
    end

    if offer then
        showOffer(offer, now, force)
    elseif reward and now < rewardUntil then
        if now >= revealAt then
            showReward(now, force)
        else
            status:SetText("Rolling...")
            roll:Disable()
            pass:Disable()
        end
    else
        currentOffer, phase, reward, flash = nil, nil, nil, nil
        frame:Hide()
    end
end

roll:SetScript("OnClick", function()
    if currentOffer and roll:IsEnabled() == 1 and api.Confirm(currentOffer) then
        action, rollAt = "roll", GetTime()
        play("rolling")
        draw(rollAt, true)
    end
end)
pass:SetScript("OnClick", function()
    if currentOffer and pass:IsEnabled() == 1 and api.Pass(currentOffer) then
        action = "pass"
        draw(GetTime(), true)
    end
end)
close:SetScript("OnClick", function()
    rewardUntil = 0
    draw(GetTime(), false)
end)

local elapsed = 0
frame:SetScript("OnUpdate", function(_, delta)
    local now = GetTime()
    animate(now)
    elapsed = elapsed + delta
    if elapsed >= .15 then
        elapsed = 0
        draw(now, false)
    end
end)
frame:SetScript("OnHide", function()
    if GameTooltip:IsOwned(icon) then GameTooltip:Hide() end
end)
frame:SetScript("OnEvent", function(self, event)
    if event == "PLAYER_LEAVING_WORLD" then
        lastResult, reward, revealAt, rewardUntil = nil, nil, nil, nil
        currentOffer, action, rollAt, phase, flash = nil, nil, nil, nil, nil
        self:Hide()
    else
        draw(GetTime(), true)
    end
end)

for _, event in ipairs({"BONUS_LOOT_ROLL_UPDATE", "BAG_UPDATE", "PLAYER_REGEN_ENABLED",
    "PLAYER_REGEN_DISABLED", "PLAYER_LEAVING_WORLD", "ITEM_DATA_LOAD_RESULT"}) do
    frame:RegisterEvent(event)
end
