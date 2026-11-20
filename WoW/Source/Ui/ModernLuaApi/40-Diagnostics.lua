if enabled(8) then
    local group = "diagnostics"
    local addonInfo, addons = GetAddOnInfo, GetNumAddOns
    local addonDeps, addonLoaded, addonLOD = GetAddOnDependencies, IsAddOnLoaded, IsAddOnLoadOnDemand
    local metadata = GetAddOnMetadata
    local refreshMemory, addonMemory = UpdateAddOnMemoryUsage, GetAddOnMemoryUsage
    local refreshCPU, addonCPU = UpdateAddOnCPUUsage, GetAddOnCPUUsage
    local scriptCPU, eventCPU, frameCPU = GetScriptCPUUsage, GetEventCPUUsage, GetFrameCPUUsage
    local scriptProfile = GetCVar and (tonumber(GetCVar("scriptProfile")) or 0) ~= 0
    local function flag(value) return value == true or value == 1 end

    expose("C_FTA", "GetModernLuaErrors", function(clear)
        local copy = {}
        for i = 1, errorCount do
            local index = (nextError - errorCount + i - 2) % maxErrors + 1
            local row = errorRing[index]
            local trace = {}
            if row.stack then
                for level = 1, #row.stack do trace[level] = row.stack[level] end
            end
            copy[i] = { message = row.message, time = row.time, stack = trace }
        end
        if clear then errorRing, nextError, errorCount = {}, 1, 0 end
        return copy
    end, group, "NATIVE_REQUIRED")

    local function describeAddon(name, caps)
        local _, title, notes, _, loadable, reason, security = addonInfo(name)
        local loading, loaded = addonLoaded(name)
        local requiredAPIs = metadata(name, "X-FTA-ModernAPIs")
        local unsupported = {}
        if type(requiredAPIs) == "string" then
            for api in requiredAPIs:gmatch("[^,%s]+") do
                if not caps[api] or not caps[api].available or caps[api].source ~= "FTA" then
                    unsupported[#unsupported + 1] = api
                end
            end
        end
        return { name = name, title = title, notes = notes, loadable = flag(loadable),
            reason = reason, security = security, enabled = native.addonState(name),
            loaded = flag(loaded), loading = flag(loading) and not flag(loaded),
            loadOnDemand = flag(addonLOD(name)), dependencies = { addonDeps(name) },
            optionalDependencies = { native.optionalDependencies(name) },
            missingModernAPIs = unsupported, memoryKB = addonMemory(name),
            cpuMs = scriptProfile and addonCPU(name) or nil }
    end

    expose("C_FTA", "GetAddonDiagnostics", function(addon)
        if type(addon) ~= "string" and type(addon) ~= "number" then
            error("add-on name or index expected", 2)
        end
        if type(addon) == "number" and (addon < 1 or addon > addons() or addon % 1 ~= 0) then return end
        local name = native.addonName(addon)
        if not name then return end
        refreshMemory()
        if scriptProfile then refreshCPU() end
        return describeAddon(name, getCapabilities())
    end, group, "NATIVE_REQUIRED")

    expose("C_FTA", "GetUIProfile", function(event, frame)
        if event ~= nil and type(event) ~= "string" then error("event must be a string", 2) end
        if frame ~= nil and (type(frame) ~= "table" or type(frame.IsObjectType) ~= "function"
            or not frame:IsObjectType("Frame")) then
            error("expected a Frame", 2)
        end
        local report = { scriptProfiling = scriptProfile == true, luaMemoryKB = collectgarbage("count"),
            modernCallbackErrors = errorCount }
        if not scriptProfile then return report end
        refreshCPU()
        report.scriptCpuMs = scriptCPU()
        if event then report.eventCpuMs, report.eventCalls = eventCPU(event) end
        if frame then report.frameCpuMs, report.frameCalls = frameCPU(frame, true) end
        return report
    end, group, "NATIVE_REQUIRED")

    local sourceStats = WXL_SourceAssetStats
    if type(sourceStats) == "function" then
        expose("C_FTA", "GetUIAssetStats", function()
            local models, textures, rejected, bytes, nativeModels, nativeFailures = sourceStats()
            return { convertedModels = models, convertedTextures = textures, rejected = rejected,
                reservedBytes = bytes, nativeModels = nativeModels, nativeModelFailures = nativeFailures }
        end, group, "NATIVE_REQUIRED")
    end
end
