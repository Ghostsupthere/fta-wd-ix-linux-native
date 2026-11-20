-- Linux port: the loaded-aura natives (server-fed aura lists) are not present yet; C_UnitAuras then reads the legacy UnitAura path.
local function findLoadedAura(...)
    if native.findUnitAura then return native.findUnitAura(...) end
end

local function queryItemLocation(location, bagQuery, inventoryQuery)
    if type(location) ~= "table" then return end
    local equipmentSlot = rawget(location, "equipmentSlotIndex")
    if equipmentSlot ~= nil then
        if rawget(location, "bagID") == nil and rawget(location, "slotIndex") == nil then
            return inventoryQuery(equipmentSlot)
        end
    else
        return bagQuery(rawget(location, "bagID"), rawget(location, "slotIndex"))
    end
end

local function locationItemLevel(location)
    return queryItemLocation(location, native.getBagItemLevel, native.getInventoryItemLevel)
end

local function locationItemBinding(location)
    return queryItemLocation(location, native.getBagItemBinding, native.getInventoryItemBinding)
end

-- Bundled addons can use the read-only service with the experimental groups
-- disabled. The global Wrath APIs and addon-supplied providers keep ownership.
expose("C_FTA", "GetItemQueryAPI", function()
    return {
        GetBagItem = native.getBagItem,
        GetInventoryItem = native.getInventoryItem,
        GetBagItemBinding = native.getBagItemBinding,
        GetInventoryItemBinding = native.getInventoryItemBinding,
        GetItemBinding = locationItemBinding,
        GetCurrentItemLevel = locationItemLevel,
        IsItemDataCachedByID = native.isItemCached,
        RequestLoadItemDataByID = native.requestItem,
    }
end, "item-query", "NATIVE_REQUIRED")

-- Bag addons keep their own item buttons and native cursor.
expose("C_FTA", "GetBagDragAPI", function()
    return {
        GetCursorBagItem = native.getCursorBagItem,
        ConsumeDragDrop = native.consumeDragDrop,
    }
end, "bag-drag", "NATIVE_REQUIRED")

-- Native Questie can use the active quest's typed data with the opt-in groups off.
expose("C_FTA", "GetQuestObjectives", native.getQuestObjectives, "quest-objectives", "NATIVE_REQUIRED")

if native.getUnitAuras then expose("C_FTA", "GetAuraQueryAPI", function()
    return {
        GetUnitAuras = native.getUnitAuras,
        GetUnitAuraFields = function(unit, filter)
            return native.getUnitAuras(unit, filter, nil, true)
        end,
    }
end, "aura-query", "NATIVE_REQUIRED") end

if enabled(2) then
    local group = "game-data"
    expose("C_QuestLog", "GetQuestObjectives", native.getQuestObjectives, group, "PARTIAL")
    local atoiMax = 2147483647
    local function integer(value, low, high)
        return type(value) == "number" and value >= low and value <= high and value % 1 == 0
    end
    local function flag(value) return value == true or value == 1 end
    local function spellKey(query)
        if type(query) == "string" and query ~= "" then
            local id = tonumber(query:match("^spell:(%d+)") or query:match("|Hspell:(%d+)") or query)
            if id ~= nil then query = id end
        end
        if type(query) == "string" and query ~= "" or integer(query, 1, atoiMax) then
            return native.resolveSpell(query)
        end
    end
    local spellInfo, spellCooldown, spellLink = GetSpellInfo, GetSpellCooldown, GetSpellLink
    local spellUsable, spellKnown, spellSlot = IsUsableSpell, IsSpellKnown, FindSpellBookSlotByID
    local function info(query)
        local id, pet = spellKey(query)
        if not id then return end
        local name, rank, icon, _, _, _, cast, minRange, maxRange
        if pet and type(query) == "string" then
            name, rank, icon, _, _, _, cast, minRange, maxRange = spellInfo(query)
        else
            name, rank, icon, _, _, _, cast, minRange, maxRange = spellInfo(id)
        end
        if name then
            return { name = name, rank = rank, iconID = icon ~= "" and icon or nil, castTime = cast,
                minRange = minRange, maxRange = maxRange, spellID = id }
        end
    end
    expose("C_Spell", "GetSpellInfo", info, group, "PARTIAL")
    expose("C_Spell", "GetSpellName", function(query)
        local id = spellKey(query)
        if id then
            local name = spellInfo(id)
            return name
        end
    end, group, "PARTIAL")
    expose("C_Spell", "GetSpellTexture", function(query)
        local id = spellKey(query)
        if id then
            local _, _, icon = spellInfo(id)
            if icon ~= "" then return icon end
        end
    end, group, "PARTIAL")
    expose("C_Spell", "DoesSpellExist", function(query) return spellKey(query) ~= nil end, group, "PARTIAL")
    expose("C_Spell", "GetSpellLink", function(query)
        local id = spellKey(query)
        if id then return spellLink(id) end
    end, group, "PARTIAL")
    expose("C_Spell", "GetSpellCooldown", function(query)
        local id = spellKey(query)
        if not id then return end
        local start, duration, usable = spellCooldown(id)
        if start then return { startTime = start, duration = duration, isEnabled = flag(usable), modRate = 1 } end
    end, group, "PARTIAL")
    expose("C_Spell", "IsSpellUsable", function(query)
        local id, pet = spellKey(query)
        if id then
            local slot = spellSlot(id, pet)
            if slot == nil and not pet then
                pet = true
                slot = spellSlot(id, pet)
            end
            if slot ~= nil then
                local usable, insufficientPower = spellUsable(slot + 1, pet and "pet" or "spell")
                return flag(usable), flag(insufficientPower)
            end
        end
        return false, false
    end, group, "PARTIAL")
    expose("C_Spell", "IsSpellKnown", function(query, pet)
        local id, spellIsPet = spellKey(query)
        if id then return flag(spellKnown(id, pet == true or spellIsPet)) end
        return false
    end, group, "PARTIAL")

    local function itemKey(query)
        if type(query) == "string" and query ~= "" then
            local linkID = query:match("^item:(%d+)") or query:match("|Hitem:(%d+)")
            local candidate = tonumber(linkID or query)
            if candidate ~= nil then
                query = candidate
            elseif not query:find("item:", 1, true) then
                return native.resolveItemName(query)
            end
        end
        if integer(query, 1, atoiMax) then return query end
    end
    local itemInfo, itemIcon = GetItemInfo, GetItemIcon
    local function itemTexture(id)
        local icon = itemIcon(id)
        if not icon then
            local _, _, _, _, _, _, _, _, _, texture = itemInfo(id)
            return texture
        end
        return icon
    end
    local function infoInstant(query)
        local id = itemKey(query)
        if not id then return end
        local equip, class, subclass = native.itemRecord(id)
        if not class then return end
        local _, _, _, _, _, itemType, itemSubType = itemInfo(id)
        return id, itemType, itemSubType, equip, itemTexture(id), class, subclass
    end
    expose("C_Item", "GetItemInfoInstant", infoInstant, group, "PARTIAL")
    expose("C_Item", "GetItemIDForItemInfo", itemKey, group, "PARTIAL")
    expose("C_Item", "GetItemInfo", function(query)
        local id = itemKey(query)
        if not id then return end
        local hasPayload = type(query) == "string" and query:find("item:", 1, true)
        local name, link, quality, level, required, itemType, subType, size, equip, icon, sell = itemInfo(hasPayload and query or id)
        if not name then return end
        -- Preserve the full owned/snapshot link, including metadata used by the
        -- equipment bridge. The Wrath formatter alone rebuilds only stock fields.
        if hasPayload and query:find("|Hitem:", 1, true) and query:find("|h", 1, true) then link = query end
        local _, class, subclass, _, bind, set = native.itemRecord(id)
        return name, link, quality, level, required, itemType, subType, size, equip, icon, sell, class, subclass, bind, nil, set
    end, group, "PARTIAL")
    expose("C_Item", "GetItemIconByID", function(query)
        local id = itemKey(query)
        if id then return itemTexture(id) end
    end, group, "PARTIAL")
    expose("C_Item", "GetCurrentItemLevel", locationItemLevel, group, "PARTIAL")
    expose("C_Item", "IsBound", function(location)
        return not not locationItemBinding(location)
    end, group, "PARTIAL")
    expose("C_Item", "IsItemDataCachedByID", function(id)
        return integer(id, 1, atoiMax) and native.isItemCached(id) or false
    end, group, "PARTIAL")
    expose("C_Item", "RequestLoadItemDataByID", function(id)
        if not integer(id, 1, atoiMax) then error("itemID must be a positive integer", 2) end
        native.requestItem(id)
    end, group, "PARTIAL")
    local function forwardItem(modern, legacy)
        if type(legacy) ~= "function" then return end
        expose("C_Item", modern, function(query, ...)
            local id = itemKey(query)
            if id then return legacy(id, ...) end
        end, group, "PARTIAL")
    end
    forwardItem("GetItemCount", GetItemCount)
    forwardItem("GetItemFamily", GetItemFamily)
    forwardItem("GetItemCooldown", GetItemCooldown)
    expose("C_Item", "GetItemGem", function(query, index)
        if type(query) == "string" and query ~= "" and integer(index, 1, 3) then return GetItemGem(query, index) end
    end, group, "PARTIAL")
    expose("C_Item", "GetItemStats", function(link, stats)
        if type(link) == "string" and itemKey(link) and (stats == nil or type(stats) == "table") then
            return GetItemStats(link, stats)
        end
    end, group, "PARTIAL")
    expose("C_FTA", "GetItemBaseData", function(query)
        local id = itemKey(query)
        if not id then return end
        local equip, class, subclass, display, bind, set, level, quality = native.itemRecord(id)
        if class then
            return { itemID = id, equipLocation = equip, classID = class, subclassID = subclass,
                displayID = display, bindType = bind, itemSetID = set, itemLevel = level, quality = quality }
        end
    end, group, "NATIVE_REQUIRED")

    local function bagKey(bag) return integer(bag, -2, 11) end
    local function bagSlot(bag, slot) return bagKey(bag) and integer(slot, 1, 65536) end
    local bagInfo, bagID = GetContainerItemInfo, GetContainerItemID
    expose("C_Container", "GetContainerItemInfo", function(bag, slot)
        if not bagSlot(bag, slot) then return end
        local icon, count, locked, quality, readable, lootable, link = bagInfo(bag, slot)
        if icon then
            return { iconFileID = icon, stackCount = count, isLocked = flag(locked), quality = quality,
                isReadable = flag(readable), hasLoot = flag(lootable), hyperlink = link, itemID = bagID(bag, slot),
                isBound = (native.getBagItemBinding(bag, slot)) }
        end
    end, group, "PARTIAL")
    local function forwardBag(name, single)
        local legacy = rawget(G, name)
        if type(legacy) ~= "function" then return end
        if single then
            expose("C_Container", name, function(bag)
                if bagKey(bag) then return legacy(bag) end
            end, group, "PARTIAL")
        else
            expose("C_Container", name, function(bag, slot)
                if bagSlot(bag, slot) then return legacy(bag, slot) end
            end, group, "PARTIAL")
        end
    end
    forwardBag("GetContainerNumSlots", true)
    forwardBag("GetContainerNumFreeSlots", true)
    forwardBag("GetBagName", true)
    forwardBag("GetContainerItemID")
    forwardBag("GetContainerItemLink")
    forwardBag("GetContainerItemCooldown")
    forwardBag("GetContainerItemDurability")
    forwardBag("PickupContainerItem")
    local questItem, useItem, splitItem = GetContainerItemQuestInfo, UseContainerItem, SplitContainerItem
    expose("C_Container", "GetContainerItemQuestInfo", function(bag, slot)
        if not bagSlot(bag, slot) then return end
        local quest, id, active = questItem(bag, slot)
        return { isQuestItem = flag(quest), questID = id, isActive = flag(active) }
    end, group, "PARTIAL")
    expose("C_Container", "UseContainerItem", function(bag, slot, unit, reagent)
        if bagSlot(bag, slot) and (unit == nil or type(unit) == "string") and not reagent then
            return useItem(bag, slot, unit)
        end
    end, group, "PARTIAL")
    expose("C_Container", "SplitContainerItem", function(bag, slot, amount)
        if bagSlot(bag, slot) and integer(amount, 1, atoiMax) then return splitItem(bag, slot, amount) end
    end, group, "PARTIAL")

    local legacyAura = UnitAura
    local knownFilters = { HELPFUL = true, HARMFUL = true, PLAYER = true, RAID = true,
        CANCELABLE = true, NOT_CANCELABLE = true }
    local function auraFilter(filter)
        if filter == nil then return "HELPFUL", true end
        if type(filter) ~= "string" then error("aura filter must be a string", 3) end
        filter = filter:upper()
        local playerFilter, helpful, harmful
        for token in filter:gmatch("[^ |]+") do
            if not knownFilters[token] then error("unsupported aura filter: " .. token, 3) end
            helpful = helpful or token == "HELPFUL"
            harmful = harmful or token == "HARMFUL"
            playerFilter = playerFilter or token == "PLAYER"
        end
        if helpful and harmful then error("choose HELPFUL or HARMFUL", 3) end
        return filter, not harmful, playerFilter
    end
    local function aura(unit, key, filter, helpful, playerFilter, id)
        if type(unit) ~= "string" or unit == "" then return nil, true end
        local name, rank, icon, count, dispel, duration, expires, caster, stealable, _, spellID
        if type(key) == "string" then
            name, rank, icon, count, dispel, duration, expires, caster, stealable, _, spellID = legacyAura(unit, key, nil, filter)
        else
            name, rank, icon, count, dispel, duration, expires, caster, stealable, _, spellID = legacyAura(unit, key, filter)
        end
        if not name then return nil, true end
        if id and spellID ~= id then return end
        local playerCast = caster == "player" or caster == "pet" or caster == "vehicle"
        -- Legacy out-of-range party data cannot identify a caster. Do not
        -- present those records as a match for a specific player's filter.
        if playerFilter and not playerCast then return end
        return { name = name, rank = rank, icon = icon, applications = count, dispelName = dispel,
            duration = duration, expirationTime = expires, sourceUnit = caster, isStealable = flag(stealable),
            spellId = spellID, isHelpful = helpful, isHarmful = not helpful,
            isFromPlayerOrPlayerPet = playerCast }
    end
    expose("C_UnitAuras", "GetAuraDataByIndex", function(unit, index, filter)
        if not integer(index, 1, 256) then return end
        local normalized, helpful, own = auraFilter(filter)
        local nativeValue, loaded = findLoadedAura(unit, normalized, index)
        if loaded then return nativeValue end
        local value = aura(unit, index, normalized, helpful, own)
        return value
    end, group, "PARTIAL")
    expose("C_UnitAuras", "GetAuraDataBySpellName", function(unit, name, filter)
        if type(name) ~= "string" or name == "" or tonumber(name) then return end
        local normalized, helpful, own = auraFilter(filter)
        local nativeValue, loaded = findLoadedAura(unit, normalized, name)
        if loaded then return nativeValue end
        local value = aura(unit, name, normalized, helpful, own)
        return value
    end, group, "PARTIAL")
    local function byID(unit, id)
        id = spellKey(id)
        if id then
            local value, loaded = findLoadedAura(unit, "HELPFUL", id, true)
            if loaded then
                if value then return value end
                value = findLoadedAura(unit, "HARMFUL", id, true)
                return value
            end
            for _, filter in ipairs({ "HELPFUL", "HARMFUL" }) do
                for i = 1, 256 do
                    local value, exhausted = aura(unit, i, filter, filter == "HELPFUL", false, id)
                    if value then return value end
                    if exhausted then break end
                end
            end
        end
    end
    expose("C_UnitAuras", "GetUnitAuraBySpellID", byID, group, "PARTIAL")
    expose("C_UnitAuras", "GetPlayerAuraBySpellID", function(id) return byID("player", id) end, group, "PARTIAL")
    expose("C_UnitAuras", "GetUnitAuras", function(unit, filter, limit, sort, direction)
        if limit ~= nil and not integer(limit, 0, atoiMax) then error("maxCount must be a nonnegative integer", 2) end
        if sort ~= nil and sort ~= 0 or direction ~= nil and direction ~= 0 then
            error("only the default aura order is supported", 2)
        end
        local normalized, helpful, own = auraFilter(filter)
        if limit == 0 then return {} end
        local rows = native.getUnitAuras and native.getUnitAuras(unit, normalized, limit and math.min(limit, 256))
        if rows then return rows end
        local result = {}
        for i = 1, 256 do
            local value, exhausted = aura(unit, i, normalized, helpful, own)
            if exhausted then break end
            if value then
                result[#result + 1] = value
                if #result == limit then break end
            end
        end
        return result
    end, group, "PARTIAL")
end
