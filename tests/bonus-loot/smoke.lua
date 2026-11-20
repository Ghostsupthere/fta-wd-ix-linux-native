-- Runs WoW/Source/Ui/BonusLootUI.lua against permissive stand-ins for the frame API and a scripted C_BonusLoot,
-- walking an offer -> roll -> reward cycle. It catches nil calls and bad arguments in the Lua, not look or feel.
-- Run: luajit tests/bonus-loot/smoke.lua   (from the reconstructed folder)
local dir = arg[0]:match("^(.*)/tests/bonus%-loot/") or "."
local h = assert(io.open(dir .. "/WoW/Source/Ui/BonusLootUI.lua")); local src = h:read("*a"); h:close()

local fails, n = 0, 0
local function check(cond, what) n = n + 1; if not cond then fails = fails + 1; print("FAIL: " .. what) end end

local env = setmetatable({}, { __index = _G })
env._G = env
local now, handlers, frames = 0, {}, {}
local enabled = true

local function proxy(name)
  local p = { __name = name, shown = false, text = "" }
  return setmetatable(p, { __index = function(t, k)
    if k == "Show" then return function(s) s.shown = true end end
    if k == "Hide" then return function(s) s.shown = false end end
    if k == "IsEnabled" then return function() return enabled and 1 or nil end end
    if k == "Enable" then return function() enabled = true end end
    if k == "Disable" then return function() enabled = false end end
    if k == "GetName" then return function() return name end end
    if k == "SetText" then return function(s, v) s.text = tostring(v) end end
    if k == "SetScript" then return function(s, ev, fn) handlers[s.__name .. ":" .. ev] = fn end end
    if k == "IsOwned" then return function() return false end end
    if type(k) ~= "string" then return nil end
    return function() return proxy(name .. "." .. k) end
  end })
end

env.UIParent = proxy("UIParent"); env.GameTooltip = proxy("GameTooltip")
env.CreateFrame = function(_, nm, _, _) local f = proxy(nm or "anon"); frames[#frames + 1] = f; return f end
env.GetTime = function() return now end
env.PlaySoundFile = function() end
env.GetItemCount = function() return 3 end
env.GetItemInfo = function() return "Elder Charm of Good Fortune", nil, 4 end
env.GetItemIcon = function() return "Interface\\Icons\\x" end
env.InCombatLockdown = function() return false end
env.GetCoinTextureString = function(c) return c .. "c" end
env.ITEM_QUALITY_COLORS = { [4] = { r = .6, g = .2, b = .9 } }
env.unpack = unpack
env.math = math

local offer, result, sent = nil, nil, {}
env.C_BonusLoot = {
  GetOffer = function() return offer end,
  GetLastResult = function() return result end,
  Confirm = function(id) sent[#sent + 1] = "T" .. id; return true end,
  Pass = function(id) sent[#sent + 1] = "P" .. id; return true end,
}
local events = {}
local errors = {}
local ok, err = pcall(setfenv(assert(loadstring(src, "BonusLootUI")), env))
check(ok, "UI script loads: " .. tostring(err))

local function tick(dt) now = now + dt; for _, k in ipairs({ "FTABonusLootFrame:OnUpdate" }) do local fn = handlers[k]; if fn then local ok2, e = pcall(fn, nil, dt); check(ok2, "OnUpdate: " .. tostring(e)) end end end
local function fire(ev) local fn = handlers["FTABonusLootFrame:OnEvent"]; check(fn ~= nil, "OnEvent handler installed"); if fn then local ok2, e = pcall(fn, frames[1], ev); check(ok2, ev .. ": " .. tostring(e)) end end

offer = { offerID = 7, charmItemID = 43641, bossName = "Kel'Thuzad", secondsRemaining = 60, pending = false }
fire("BONUS_LOOT_ROLL_UPDATE"); tick(.2)
check(frames[1].shown, "offer shows the frame")
local roll = handlers["FTABonusLootFrameRoll:OnClick"]
check(roll ~= nil, "Roll button has a click handler")
if roll then local ok2, e = pcall(roll); check(ok2, "Roll click: " .. tostring(e)) end
check(sent[1] == "T7", "Roll sends a confirm for offer 7")
offer.pending = true; tick(.2)
offer = nil; result = { offerID = 7, kind = "item", itemID = 40000, count = 1, randomProperty = 0 }
fire("BONUS_LOOT_ROLL_UPDATE"); tick(1.0); tick(.2)
check(frames[1].shown, "reward shows the frame")
result = { offerID = 8, kind = "gold", copper = 12345 }
fire("BONUS_LOOT_ROLL_UPDATE"); tick(.2); tick(.2)
fire("PLAYER_LEAVING_WORLD")
check(not frames[1].shown, "leaving the world hides the frame")
offer = { offerID = 9, charmItemID = 43641, bossName = "Anub'Rekhan", secondsRemaining = 30, pending = false, error = "charm" }
fire("BAG_UPDATE"); tick(.2)
check(frames[1].shown, "offer with an error still shows")
print(string.format("%d checks, %d failed", n, fails))
os.exit(fails == 0 and 0 or 1)
