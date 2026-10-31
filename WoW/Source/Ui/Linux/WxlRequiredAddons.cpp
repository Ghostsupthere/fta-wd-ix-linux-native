// Native port of the supplied wxl-required-addons (GPL-3.0-or-later, Copyright (C) 2026
// WarcraftXL). The policy script below is the module's own, unchanged; see WxlRequiredAddons.h.
#include "WoW/Source/Ui/Linux/WxlRequiredAddons.h"

#if defined(WOWDECOMP_LINUX)

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Ui/AddOns.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"

namespace {

// The stock in-world UI has GetAddOnMetadata, the GlueXML context does not, but both read the same
// parsed add-on catalog. The policy needs it at character select, where add-ons are chosen.
int GlueGetAddOnMetadata(lua_State* L) {
    const char* name = nullptr;
    if (lua_isnumber(L, 1)) {
        name = GetAddOnByIndex(static_cast<uint32_t>(lua_tonumber(L, 1)) - 1);
        if (!name) return luaL_error(L, "AddOn index must be in the range of 1 to %d", GetNumAddOns());
    } else if (!lua_isstring(L, 1) || !(name = lua_tolstring(L, 1, nullptr))) {
        return luaL_error(L, "Usage: GetAddOnMetadata(index or \"name\", \"variable\")");
    }
    if (!lua_isstring(L, 2)) return luaL_error(L, "Usage: GetAddOnMetadata(index or \"name\", \"variable\")");
    lua_pushstring(L, GetAddOnMetadata(name, lua_tolstring(L, 2, nullptr)));
    return 1;
}

constexpr char kPolicyScript[] = R"lua(
do
    local TAG = "X-WarcraftXL-Required"
    local policy = rawget(_G, "WarcraftXL_RequiredAddons")
    if type(policy) ~= "table" then
        policy = {}
        _G.WarcraftXL_RequiredAddons = policy
    end

    -- A state can receive this source more than once after a world transition.
    -- Keep the original native functions and the light polling frame singular.
    if policy._wxlInstalled then
        if type(policy.InstallHooks) == "function" then policy:InstallHooks() end
        if type(policy.Enforce) == "function" then policy:Enforce() end
        if type(policy.LockVisibleRows) == "function" then policy:LockVisibleRows() end
        return
    end
    policy._wxlInstalled = true
    policy.tag = TAG
    policy._lockedButtons = policy._lockedButtons or setmetatable({}, { __mode = "k" })

    local function IsEnabled(value)
        return value == true or value == 1
    end

    local function IsRequiredValue(value)
        if value == nil then return false end
        local normalized = string.lower(tostring(value))
        normalized = string.gsub(normalized, "^%s+", "")
        normalized = string.gsub(normalized, "%s+$", "")
        return normalized == "1" or normalized == "true" or normalized == "yes"
            or normalized == "on" or normalized == "required" or normalized == "locked"
    end

    function policy:GetAddonCount()
        if type(GetNumAddOns) ~= "function" then return 0 end
        local count = tonumber(GetNumAddOns()) or 0
        return math.max(0, math.floor(count))
    end

    function policy:GetName(addon)
        if type(GetAddOnInfo) ~= "function" then return nil end

        -- GlueXML can execute before the client has built its addon catalog.  In
        -- that state GetNumAddOns() is 0, while the partially constructed rows
        -- can still expose GetID() == 0.  Passing that through to GetAddOnInfo
        -- produces the "index must be in the range of 1 to 0" popup.
        local count = self:GetAddonCount()
        if count == 0 then return nil end

        if type(addon) == "number" then
            local index = math.floor(addon)
            if index < 1 or index > count then return nil end
            addon = index
        elseif type(addon) == "string" then
            if addon == "" then return nil end

            -- Do not rely on this legacy client's undocumented string overload.
            -- Resolve a manager-supplied addon name using only known-valid
            -- numeric indexes instead.
            for index = 1, count do
                local name = GetAddOnInfo(index)
                if name == addon then return name end
            end
            return nil
        else
            return nil
        end

        local name = GetAddOnInfo(addon)
        if type(name) ~= "string" or name == "" then return nil end
        return name
    end

    function policy:IsRequiredName(name)
        if not name or type(GetAddOnMetadata) ~= "function" then return false end
        return IsRequiredValue(GetAddOnMetadata(name, TAG))
    end

    function policy:IsRequired(addon)
        return self:IsRequiredName(self:GetName(addon))
    end

    function policy:GetRequiredAddons()
        local result = {}
        for index = 1, self:GetAddonCount() do
            if self:IsRequired(index) then
                local name = self:GetName(index)
                if name then result[#result + 1] = name end
            end
        end
        return result
    end

    function policy:Enforce()
        if self._enforcing or type(GetAddOnInfo) ~= "function" or type(EnableAddOn) ~= "function" then
            return
        end

        local count = self:GetAddonCount()
        if count == 0 then return end

        self._enforcing = true
        for index = 1, count do
            if self:IsRequired(index) then
                local name, _, _, enabled = GetAddOnInfo(index)
                if type(name) == "string" and name ~= "" and not IsEnabled(enabled) then
                    -- Use the stock API. It updates the active character's addon
                    -- selection exactly as the normal AddOns screen does.
                    pcall(EnableAddOn, name)
                end
            end
        end
        self._enforcing = false
    end

    function policy:LockVisibleRows()
        if type(GetAddOnInfo) ~= "function" then return end

        local requiredNames = {}
        for index = 1, self:GetAddonCount() do
            local name = GetAddOnInfo(index)
            if type(name) == "string" and self:IsRequiredName(name) then
                requiredNames[name] = true
            end
        end

        local function ResolveName(button)
            if not button then return nil end
            local parent = button.GetParent and button:GetParent()
            return self:GetName(button.addon)
                or self:GetName(button.addonIndex)
                or self:GetName(parent and parent.addon)
                or self:GetName(parent and parent.addonIndex)
                or self:GetName(button.GetID and button:GetID())
        end

        local function Lock(button)
            if not button then return end

            local name = ResolveName(button)
            if name and requiredNames[name] then
                self._lockedButtons[button] = name
                if button.SetChecked then button:SetChecked(true) end
                if button.Disable then button:Disable() end
            elseif self._lockedButtons[button] then
                -- ACP and Glue both recycle visible rows while scrolling.  A
                -- checkbox locked for a required addon must be released when
                -- that same row starts displaying an optional addon.
                self._lockedButtons[button] = nil
                if button.Enable then button:Enable() end
            end
        end

        -- The stock frame and ACP use these standard row names. Unknown addon
        -- managers remain protected by the DisableAddOn wrapper and re-check.
        for row = 1, 80 do
            Lock(_G["AddonListEntry" .. row .. "Enabled"])
            Lock(_G["ACP_AddonListEntry" .. row .. "Enabled"])
        end
    end

    function policy:InstallHooks()
        -- Native addon APIs may not exist yet when GlueXML first creates its
        -- FrameScript state. Install lazily, and re-wrap if a manager replaces
        -- one of these globals later in startup.
        local currentDisable = rawget(_G, "DisableAddOn")
        if type(currentDisable) == "function" and currentDisable ~= self._disableWrapper then
            local originalDisable = currentDisable
            local wrapper = function(addon, ...)
                if policy:IsRequired(addon) then
                    policy:Enforce()
                    policy:LockVisibleRows()
                    return
                end
                return originalDisable(addon, ...)
            end
            self._originalDisable = originalDisable
            self._disableWrapper = wrapper
            _G.DisableAddOn = wrapper
        end

        local currentDisableAll = rawget(_G, "DisableAllAddOns")
        if type(currentDisableAll) == "function" and currentDisableAll ~= self._disableAllWrapper then
            local originalDisableAll = currentDisableAll
            local wrapper = function(...)
                local results = { originalDisableAll(...) }
                policy:Enforce()
                policy:LockVisibleRows()
                return unpack(results)
            end
            self._originalDisableAll = originalDisableAll
            self._disableAllWrapper = wrapper
            _G.DisableAllAddOns = wrapper
        end

        -- Character-select GlueXML defines these after the native script is
        -- registered. Hooking the update gives us the real checkbox IDs after
        -- scrolling/filtering, and hooking Enable prevents even a transient
        -- unchecked state for required rows.
        local currentGlueUpdate = rawget(_G, "AddonList_Update")
        if type(currentGlueUpdate) == "function" and currentGlueUpdate ~= self._glueUpdateWrapper then
            local originalGlueUpdate = currentGlueUpdate
            local wrapper = function(...)
                local results = { originalGlueUpdate(...) }
                policy:Enforce()
                policy:LockVisibleRows()
                return unpack(results)
            end
            self._glueUpdateWrapper = wrapper
            _G.AddonList_Update = wrapper
        end

        local currentGlueEnable = rawget(_G, "AddonList_Enable")
        if type(currentGlueEnable) == "function" and currentGlueEnable ~= self._glueEnableWrapper then
            local originalGlueEnable = currentGlueEnable
            local wrapper = function(addon, enabled, ...)
                if not IsEnabled(enabled) and policy:IsRequired(addon) then
                    policy:Enforce()
                    policy:LockVisibleRows()
                    return
                end
                local results = { originalGlueEnable(addon, enabled, ...) }
                policy:Enforce()
                policy:LockVisibleRows()
                return unpack(results)
            end
            self._glueEnableWrapper = wrapper
            _G.AddonList_Enable = wrapper
        end

        -- ACP owns methods rather than globals, so hook its refresh and toggle
        -- methods once the addon has loaded in the in-world FrameScript state.
        local addonManager = rawget(_G, "ACP")
        if type(addonManager) == "table" then
            local currentAcpUpdate = addonManager.AddonList_OnShow
            if type(currentAcpUpdate) == "function" and currentAcpUpdate ~= self._acpUpdateWrapper then
                local originalAcpUpdate = currentAcpUpdate
                local wrapper = function(owner, ...)
                    local results = { originalAcpUpdate(owner, ...) }
                    policy:Enforce()
                    policy:LockVisibleRows()
                    return unpack(results)
                end
                self._acpUpdateWrapper = wrapper
                addonManager.AddonList_OnShow = wrapper
            end

            local currentAcpEnable = addonManager.AddonList_Enable
            if type(currentAcpEnable) == "function" and currentAcpEnable ~= self._acpEnableWrapper then
                local originalAcpEnable = currentAcpEnable
                local wrapper = function(owner, addon, enabled, ...)
                    if not IsEnabled(enabled) and policy:IsRequired(addon) then
                        policy:Enforce()
                        policy:LockVisibleRows()
                        return
                    end
                    local results = { originalAcpEnable(owner, addon, enabled, ...) }
                    policy:Enforce()
                    policy:LockVisibleRows()
                    return unpack(results)
                end
                self._acpEnableWrapper = wrapper
                addonManager.AddonList_Enable = wrapper
            end
        end
    end

    policy:InstallHooks()
    policy:Enforce()
    policy:LockVisibleRows()

    if type(CreateFrame) == "function" then
        local frame = CreateFrame("Frame")
        local elapsed = 0
        frame:SetScript("OnUpdate", function(_, delta)
            elapsed = elapsed + (tonumber(delta) or 0)
            if elapsed < 0.40 then return end
            elapsed = 0
            policy:InstallHooks()
            policy:Enforce()
            policy:LockVisibleRows()
        end)
        policy._frame = frame
    end
end
)lua";

}  // namespace

void WxlRunRequiredAddonsPolicy(bool glue) {
    static const bool enabled = WxlProfileEnabled("WXL_REQUIRED_ADDONS", true);
    if (!enabled) return;
    if (glue) FrameScript_RegisterFunction("GetAddOnMetadata", &GlueGetAddOnMetadata);
    // Runs in the current FrameScript context, which is the one that just finished loading its XML.
    // The script is idempotent: a context that already holds the policy table only re-checks it.
    FrameScript_Execute(kPolicyScript, "wxl-required-addons", nullptr);
    WxlNativeDiagnostic("required-addons: TOC policy installed (tag=X-WarcraftXL-Required)");
}

#endif  // WOWDECOMP_LINUX
