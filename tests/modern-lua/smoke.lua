-- Loads the four embedded phase files with stand-ins for the client's globals and checks what the API exposes.
-- Run: luajit tests/modern-lua/smoke.lua   (from the reconstructed folder; the same code runs in the client with real natives)
local dir = arg[0]:match("^(.*)/tests/modern%-lua/") or "."
local files = { "00-Core", "10-Foundation", "20-GameData", "40-Diagnostics" }
local src = {}
for _, f in ipairs(files) do
  local h = assert(io.open(dir .. "/WoW/Source/Ui/ModernLuaApi/" .. f .. ".lua")); src[#src + 1] = h:read("*a"); h:close()
end
local script = table.concat(src, "\n")

local fails, n = 0, 0
local function check(cond, what) n = n + 1; if not cond then fails = fails + 1; print("FAIL: " .. what) end end

local function run(groups, withNatives)
  local now, onUpdate = 0, nil
  local env = setmetatable({}, { __index = _G })
  env._G = env
  env.GetTime = function() return now end
  env.UIParent = {}
  env.geterrorhandler = function() return function(e) env.lastError = e end end
  env.CreateFrame = function()
    local f = { shown = false }
    function f:Show() self.shown = true end
    function f:Hide() self.shown = false end
    function f:SetScript(_, fn) onUpdate = fn end
    function f:RegisterEvent() end
    function f:UnregisterEvent() end
    return f
  end
  env.GetNumAddOns = function() return 2 end
  env.GetAddOnInfo = function(a) return type(a) == "string" and a or "Name" .. a, "Title", "Notes", 1, 1, nil, "INSECURE" end
  env.IsAddOnLoaded = function() return 1, 1 end
  env.IsAddOnLoadOnDemand = function() return nil end
  env.LoadAddOn = function() return 1 end
  env.GetAddOnMetadata = function() end
  env.GetAddOnDependencies = function() end
  env.GetSpellInfo = function(id) if id == 133 then return "Fireball", "Rank 1", "icon", 0, 0, 0, 3500, 0, 35 end end
  env.UnitAura = function() end
  env.GetItemInfo = function(id) if id == 6948 then return "Hearthstone", "link", 1, 1, 1, "Miscellaneous", "Junk", 1, "", "tex", 0 end end
  env.GetItemIcon = function() return "icon" end
  env.GetCVar = function() return "0" end
  env.UpdateAddOnMemoryUsage = function() end
  env.GetAddOnMemoryUsage = function() return 12 end
  local native = {
    bindCallback = function(fn) return function(...) local r = { pcall(fn, ...) }; return unpack(r) end end,
    addonName = function(a) return type(a) == "string" and a or "Name" .. a end,
    addonState = function() return 2 end,
    optionalDependencies = function() return "A", "B" end,
    resolveSpell = function(q) if q == 133 or q == "Fireball" then return 133, false end end,
    itemRecord = function(id) if id == 6948 then return "INVTYPE_NON_EQUIP", 15, 0, 2, 0, 0, 1, 1 end end,
    isItemCached = function() return true end,
    requestItem = function() return true end,
    saveAddonStates = function() end, setAddonState = function() end,
  }
  local chunk = assert(loadstring(script, "@modern"))
  setfenv(chunk, env)
  chunk(groups, native)
  return env, function(t) now = now + t; if onUpdate then onUpdate() end end
end

-- groups 0: only the C_FTA core and service tables
local g = run(0)
check(g.C_FTA and g.C_FTA.GetModernAPIVersion() == 1, "core version")
check(g.C_FTA.GetTimerAPI and g.C_FTA.GetItemQueryAPI, "service tables present")
check(g.C_Timer == nil and g.C_Item == nil and g.C_Spell == nil and g.Mixin == nil, "no experimental surface at groups 0")
check(g.C_QuestLog == nil, "no empty C_QuestLog")
check(g.C_FTA.GetAuraQueryAPI == nil, "no aura query service without the native")

-- groups 15: everything ported
local e, tick = run(15)
check(e.C_Timer and e.C_Timer.After and e.C_Timer.NewTicker, "C_Timer")
check(e.C_QuestLog == nil, "C_QuestLog stays absent")
local fired = 0
e.C_Timer.After(1, function() fired = fired + 1 end)
local ticks = 0
local tk = e.C_Timer.NewTicker(0.5, function() ticks = ticks + 1 end, 3)
tick(0.4); check(fired == 0 and ticks == 0, "timers wait")
tick(0.2); check(ticks == 1, "first tick")
tick(0.6); check(fired == 1, "after fires once")
tick(5); tick(5); tick(5); check(ticks == 3, "ticker stops after 3 (got " .. ticks .. ")")
check(e.Mixin({ a = 1 }, { b = 2 }).b == 2, "Mixin")
local info = e.C_Spell.GetSpellInfo(133)
check(info and info.name == "Fireball" and info.spellID == 133, "C_Spell.GetSpellInfo")
check(e.C_Spell.DoesSpellExist("spell:133") and not e.C_Spell.DoesSpellExist(999999), "DoesSpellExist")
local a, b, c, d = e.C_Item.GetItemInfoInstant(6948)
check(a == 6948 and b == "Miscellaneous" and c == "Junk" and d == "INVTYPE_NON_EQUIP", "GetItemInfoInstant")
check(e.C_AddOns.GetNumAddOns() == 2 and e.C_AddOns.GetAddOnOptionalDependencies(1) == "A", "C_AddOns")
check(not pcall(e.C_AddOns.GetAddOnInfo, 3), "add-on index range checked")
check(e.C_UnitAuras.GetUnitAuras("player") ~= nil and #e.C_UnitAuras.GetUnitAuras("player") == 0, "C_UnitAuras falls back to legacy")
local caps = e.C_FTA.GetModernAPICapabilities()
check(caps["C_Timer.After"] and caps["C_Timer.After"].source == "FTA", "capability report")
local diag = e.C_FTA.GetAddonDiagnostics("Name1")
check(diag and diag.name == "Name1" and #diag.optionalDependencies == 2, "diagnostics")
-- an error in a timer callback reaches the error handler and the ring
e.C_Timer.After(0, function() error("boom") end); tick(1)
check(e.lastError and tostring(e.lastError):find("boom"), "timer error reported")
local errs = e.C_FTA.GetModernLuaErrors()
check(#errs == 1 and errs[1].message:find("boom"), "error ring")
print(("%d checks, %d failed"):format(n, fails))
os.exit(fails == 0 and 0 or 1)
