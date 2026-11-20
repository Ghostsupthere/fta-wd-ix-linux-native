do
    local heap, pending = {}, {}
    local count, serial = 0, 0
    local driver, dispatching
    local states = setmetatable({}, { __mode = "k" })

    local function comesBefore(a, b)
        return a.due < b.due or (a.due == b.due and a.serial < b.serial)
    end

    local function move(index, node)
        heap[index], node.index = node, index
    end

    local function moveUp(index, node)
        while index > 1 do
            local parent = math.floor(index / 2)
            if not comesBefore(node, heap[parent]) then break end
            move(index, heap[parent])
            index = parent
        end
        move(index, node)
    end

    local function insert(node)
        count = count + 1
        moveUp(count, node)
    end

    local function remove(node)
        local index = node.index
        if not index then return end
        local last = heap[count]
        heap[count], node.index = nil, nil
        count = count - 1
        if index > count then return end
        if index > 1 and comesBefore(last, heap[math.floor(index / 2)]) then
            moveUp(index, last)
            return
        end
        while index * 2 <= count do
            local child = index * 2
            if child < count and comesBefore(heap[child + 1], heap[child]) then child = child + 1 end
            if not comesBefore(heap[child], last) then break end
            move(index, heap[child])
            index = child
        end
        move(index, last)
    end

    local function queue(node, now)
        serial = serial + 1
        node.serial, node.due = serial, now + node.duration
        if dispatching then
            pending[#pending + 1] = node
        else
            insert(node)
            driver:Show()
        end
    end

    local function dispatchTimers()
        local now = GetTime()
        dispatching = true
        -- Work scheduled by callbacks starts on the next frame. Skip missed repetitions.
        for _ = 1, 256 do
            local node = heap[1]
            if not node or node.due > now then break end
            remove(node)
            local callback, handle = node.callback, node.handle
            if node.remaining == 1 then
                node.callback, node.handle = nil, nil
            elseif node.remaining then
                node.remaining = node.remaining - 1
            end

            local ok, failure, stack
            if handle then ok, failure, stack = callback(handle) else ok, failure, stack = callback() end
            if not ok then reportError(failure, stack) end
            if node.callback then queue(node, GetTime()) end
        end
        dispatching = false
        for i = 1, #pending do
            local node = pending[i]
            if node.callback then insert(node) end
            pending[i] = nil
        end
        if count == 0 then driver:Hide() end
    end

    -- Keep addon housekeeping ticking while the user hides UIParent.
    driver = CreateFrame("Frame")
    driver:Hide()
    driver:SetScript("OnUpdate", dispatchTimers)

    local methods = {}
    function methods:Cancel()
        local node = states[self]
        if not node then error("invalid timer handle", 2) end
        node.cancelled, node.callback, node.handle = true, nil, nil
        remove(node)
        if not dispatching and count == 0 then driver:Hide() end
    end
    function methods:IsCancelled()
        local node = states[self]
        if not node then error("invalid timer handle", 2) end
        return node.cancelled == true
    end
    local timerMeta = { __index = methods, __metatable = false }

    local function schedule(duration, callback, repeats, wantHandle)
        if checkFinite(duration, "duration", 4) < 0 then error("duration must be nonnegative", 3) end
        if type(callback) ~= "function" then error("callback must be a function", 3) end
        if repeats and (checkFinite(repeats, "iterations", 4) < 1 or repeats % 1 ~= 0 or repeats > 2147483647) then
            error("iterations must be a positive integer", 3)
        end

        local handle = wantHandle and setmetatable({}, timerMeta) or nil
        local node = { duration = duration, callback = bindCallback(callback), handle = handle, remaining = repeats }
        if handle then states[handle] = node end
        queue(node, GetTime())
        return handle
    end

    local function after(duration, callback, methodCallback)
        if type(duration) == "table" then duration, callback = callback, methodCallback end
        schedule(duration, callback, 1, false)
    end
    local function newTimer(duration, callback, methodCallback)
        if type(duration) == "table" then duration, callback = callback, methodCallback end
        return schedule(duration, callback, 1, true)
    end
    local function newTicker(duration, callback, iterations, methodIterations)
        if type(duration) == "table" then duration, callback, iterations = callback, iterations, methodIterations end
        if iterations == false then error("iterations must be a positive integer", 2) end
        return schedule(duration, callback, iterations, true)
    end

    -- Each consumer can keep its own API table, but all use the same scheduler.
    expose("C_FTA", "GetTimerAPI", function()
        return { After = after, NewTimer = newTimer, NewTicker = newTicker }
    end, "core", "NATIVE_REQUIRED")
    if enabled(1) then
        expose("C_Timer", "After", after, "foundation", "EMULATED")
        expose("C_Timer", "NewTimer", newTimer, "foundation", "EMULATED")
        expose("C_Timer", "NewTicker", newTicker, "foundation", "EMULATED")
    end
end

if enabled(1) then
    local group = "foundation"
    local function mixin(target, ...)
        if type(target) ~= "table" then error("mixin target must be a table", 2) end
        for i = 1, select("#", ...) do
            local source = select(i, ...)
            if type(source) ~= "table" then error("mixin source must be a table", 2) end
            for key, value in pairs(source) do target[key] = value end
        end
        return target
    end
    local function fromMixins(...) return mixin({}, ...) end
    local function initFromMixin(source, ...)
        local object = fromMixins(source)
        object:Init(...)
        return object
    end

    expose("_G", "Mixin", mixin, group, "FULL")
    expose("_G", "CreateFromMixins", fromMixins, group, "FULL")
    expose("_G", "CreateAndInitFromMixin", initFromMixin, group, "FULL")

    local function legacyFlag(value) return value == true or value == 1 end
    local addonInfo, addonLoad = GetAddOnInfo, LoadAddOn
    local addonLoaded, addonLOD = IsAddOnLoaded, IsAddOnLoadOnDemand

    -- The stock index adapters cast directly to integers. Check arguments before forwarding.
    local function addonKey(value)
        if type(value) == "number" then
            if value < 1 or value > GetNumAddOns() or value % 1 ~= 0 then error("add-on index out of range", 3) end
        elseif type(value) ~= "string" then
            error("add-on name or index expected", 3)
        end
        return value
    end
    local function exposeAddOn(name, implementation, contract)
        expose("C_AddOns", name, implementation, group, contract or "PARTIAL")
    end
    local function forwardAddOn(name)
        local legacy = rawget(G, name)
        if type(legacy) == "function" then
            exposeAddOn(name, function(addon, ...) return legacy(addonKey(addon), ...) end)
        end
    end
    exposeAddOn("GetNumAddOns", GetNumAddOns, "FULL")
    forwardAddOn("GetAddOnMetadata")
    forwardAddOn("GetAddOnDependencies")
    exposeAddOn("GetAddOnOptionalDependencies", function(addon)
        return native.optionalDependencies(addonKey(addon))
    end)
    exposeAddOn("GetAddOnInfo", function(addon)
        local name, title, notes, _, loadable, reason, security = addonInfo(addonKey(addon))
        return name, title, notes, legacyFlag(loadable), reason, security
    end)
    exposeAddOn("IsAddOnLoaded", function(addon)
        local loading, loaded = addonLoaded(addonKey(addon))
        return legacyFlag(loading) or legacyFlag(loaded), legacyFlag(loaded)
    end)
    exposeAddOn("IsAddOnLoadOnDemand", function(addon) return legacyFlag(addonLOD(addonKey(addon))) end)
    exposeAddOn("LoadAddOn", function(addon)
        local loaded, reason = addonLoad(addonKey(addon))
        return legacyFlag(loaded), reason
    end)
    exposeAddOn("GetAddOnEnableState", function(addon, character)
        return native.addonState(addonKey(addon), character)
    end)
    exposeAddOn("EnableAddOn", function(addon, character)
        native.setAddonState(addonKey(addon), character, true)
    end)
    exposeAddOn("DisableAddOn", function(addon, character)
        native.setAddonState(addonKey(addon), character, false)
    end)
    exposeAddOn("SaveAddOns", native.saveAddonStates)

    local function createRegistry(useEvents)
        local subscriptions = {}
        local frame = useEvents and CreateFrame("Frame", nil, UIParent)
        local registry = {}

        function registry:RegisterCallback(event, callback, owner)
            if type(event) ~= "string" or event == "" or type(callback) ~= "function" then
                error("expected event name and callback function", 2)
            end
            local current = subscriptions[event]
            owner = owner or callback
            local replacement = {}
            if current then
                for i = 1, #current do
                    local entry = current[i]
                    if entry.owner == owner then
                        entry.active = false
                    else
                        replacement[#replacement + 1] = entry
                    end
                end
            elseif frame then
                frame:RegisterEvent(event)
            end
            replacement[#replacement + 1] = { owner = owner, callback = bindCallback(callback), active = true }
            subscriptions[event] = replacement
        end

        function registry:UnregisterCallback(event, owner)
            local current = subscriptions[event]
            if not current then return end
            local replacement = {}
            for i = 1, #current do
                local entry = current[i]
                if entry.owner == owner then
                    entry.active = false
                else
                    replacement[#replacement + 1] = entry
                end
            end
            if #replacement > 0 then
                subscriptions[event] = replacement
            else
                subscriptions[event] = nil
                if frame then frame:UnregisterEvent(event) end
            end
        end

        function registry:TriggerEvent(event, ...)
            local snapshot = subscriptions[event]
            if not snapshot then return end
            for i = 1, #snapshot do
                local entry = snapshot[i]
                if entry.active then
                    local ok, failure, stack = entry.callback(entry.owner, ...)
                    if not ok then reportError(failure, stack) end
                end
            end
        end

        function registry:ClearCallbacks()
            for event, entries in pairs(subscriptions) do
                for i = 1, #entries do entries[i].active = false end
                if frame then frame:UnregisterEvent(event) end
            end
            subscriptions = {}
        end

        if frame then
            frame:SetScript("OnEvent", function(_, event, ...)
                registry:TriggerEvent(event, ...)
            end)
        end
        return registry
    end

    expose("C_FTA", "CreateCallbackRegistry", function() return createRegistry(false) end, group, "EMULATED")
    expose("C_FTA", "CreateEventRegistry", function() return createRegistry(true) end, group, "EMULATED")
end
