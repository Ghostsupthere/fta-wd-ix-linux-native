local panel = CreateFrame("Frame", "RoundPortraitFixture", UIParent)
panel:SetSize(592, 112)
panel:SetPoint("CENTER", UIParent, "CENTER", 0, 95)
panel:Hide()

local samples = {}
local elapsed, nextSwap
local function setUnit(sample, unit)
    sample.model:ClearModel()
    SetPortraitTexture(sample.face, unit)
    sample.model:SetUnit(unit)
end

local function build()
    for i = 1, 8 do
        local frame = CreateFrame("Frame", nil, panel)
        frame:SetSize(64, 64)
        frame:SetPoint("TOPLEFT", 8 + (i - 1) * 73, -16)
        local background = frame:CreateTexture(nil, "BACKGROUND")
        background:SetAllPoints()
        background:SetTexture(.85, 0, .75, 1)
        local face = frame:CreateTexture(nil, "ARTWORK")
        face:SetAllPoints()
        local model = CreateFrame("PlayerModel", nil, frame)
        model:SetAllPoints()
        model:SetFrameLevel(frame:GetFrameLevel())
        local marker = CreateFrame("Frame", nil, frame)
        marker:SetFrameLevel(frame:GetFrameLevel() + 1)
        marker:SetAllPoints()
        local line = marker:CreateTexture(nil, "OVERLAY")
        line:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 0, -5)
        line:SetSize(64, 3)
        line:SetTexture(.1, 1, .12, 1)
        samples[i] = { model = model, face = face }
    end
end

local function show(count)
    panel:Show()
    if #samples == 0 then build() end
    for i, sample in ipairs(samples) do
        sample.model:Hide()
        SetPortraitTexture(sample.face, "player")
        if i <= count and sample.model.SetClipCircle and sample.model:SetClipCircle(true) then
            sample.model:Show()
            setUnit(sample, "player")
        end
    end
end

panel:SetScript("OnUpdate", function(_, dt)
    if not elapsed then return end
    elapsed = elapsed + dt
    if elapsed >= 60 then
        elapsed = nil
        print("Round portrait fixture finished the 60-second retarget loop.")
    elseif elapsed >= nextSwap then
        local tick = math.floor(elapsed * 8)
        nextSwap = (tick + 1) / 8
        local unit = tick % 2 == 0 and "target" or "player"
        if not UnitExists(unit) then unit = "player" end
        for _, sample in ipairs(samples) do setUnit(sample, unit) end
    end
end)

SLASH_ROUNDPORTRAITFIXTURE1 = "/roundportrait"
SlashCmdList.ROUNDPORTRAITFIXTURE = function(message)
    elapsed = nil
    if message == "hide" then
        panel:Hide()
    elseif message == "retarget" then
        if not UnitExists("target") then print("Select a target for the retarget loop."); return end
        show(8)
        if samples[1].model:IsShown() then elapsed, nextSwap = 0, 0 end
    else
        local count = tonumber(message)
        if count ~= 0 and count ~= 1 and count ~= 8 then
            print("Round portrait fixture: /roundportrait 0, 1, 8, retarget, or hide")
            return
        end
        show(count)
    end
end
