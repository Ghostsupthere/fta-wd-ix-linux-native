-- Party frame power bars must follow this client's power events (ticket #14: mana looked frozen between pulls).
-- Only the packaged addon carries the Dragonflight party module, so a source directory skips this check.
local M = ...
local file
for _, path in ipairs({ "modules/DragonflightUI/Modules/partyframes.lua", "modules/dragonflightui/modules/partyframes.lua" }) do
    local handle = io.open(M.directory .. "/" .. path, "rb")
    if handle then file = handle:read("*a"); handle:close(); break end
end
if not file then print("Party frame power events skipped (no party module in this directory)") return end
for _, event in ipairs({ "UNIT_MANA", "UNIT_ENERGY", "UNIT_RAGE", "UNIT_RUNIC_POWER", "UNIT_MAXMANA" }) do
    assert(file:find('"' .. event .. '"', 1, true), "party frames do not register " .. event)
end
assert(file:find("partyUnitEvents[event]", 1, true), "party frames do not refresh on power events")
assert(loadstring(file), "party frame module does not compile")
print("Party frame power events passed")
