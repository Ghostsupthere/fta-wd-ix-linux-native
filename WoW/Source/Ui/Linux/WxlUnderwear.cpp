// Native port of the supplied wxl-underwear (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL).
// The Lua contract and the profile rules follow scripts/wxl-underwear; see WxlUnderwear.h for how
// the three native detours became hooks inside CCharacterComponent.
#include "WoW/Source/Ui/Linux/WxlUnderwear.h"

#if defined(WOWDECOMP_LINUX)

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/ModernAssets/WxlUnderwear/UnderwearPaths.hpp"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Common/TextureCache.h"
#include "WoW/Source/DB/Rec/CharSectionsRec.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"

namespace {

constexpr size_t kMaxOwners = 129;

struct Profile {
    uint32_t top = 0;
    uint32_t bottom = 0;
    std::string topPath;
    std::string bottomPath;
    HTEXTURECACHE lowerTexture = nullptr;  // one owned cache reference; requests take their own
    CharSectionsRec record;                // the override handed to the component, copied per lookup
    const void* appliedModel = nullptr;    // comparison token only; never dereferenced
    bool dirty = true;
};

// Lua and character texture composition run on the client main thread. Never destroyed: a static
// destructor would release texture-cache entries after the cache is gone at exit.
std::unordered_map<uint64_t, Profile>& Profiles() {
    static auto* profiles = new std::unordered_map<uint64_t, Profile>();
    return *profiles;
}

bool g_enabled = true;

CCharacterComponent* ComponentFor(uint64_t guid) {
    auto* unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(WOWGUID{guid}, TYPE_PLAYER, __FILE__, __LINE__));
    if (!unit) return nullptr;
    CCharacterComponent* component = unit->m_characterComponent;
    // The component must still describe this unit's own model, not a recycled one.
    if (!component || component->m_data.m_preferences.m_raceID > 36 || component->m_data.m_preferences.m_sexID > 1 ||
        component->m_data.m_model != unit->m_worldModel) {
        return nullptr;
    }
    return component;
}

void ReleaseLower(Profile& profile) {
    if (profile.lowerTexture) TextureCacheDestroyTexture(profile.lowerTexture);
    profile.lowerTexture = nullptr;
}

void Refresh(uint64_t guid, Profile& profile) {
    CCharacterComponent* component = ComponentFor(guid);
    if (!component) return;
    const void* model = component->m_data.m_model;
    if (!profile.dirty && profile.appliedModel == model) return;
    // Stock NPC/full-skin presentations intentionally keep their baked underwear.
    if (!(component->m_data.m_flags & 1)) {
        component->SetSkinColor(static_cast<int32_t>(component->m_data.m_preferences.m_skinID), true, false, nullptr);
        component->RenderPrep(0);
    }
    profile.appliedModel = model;
    profile.dirty = false;
}

bool Number(lua_State* L, int index, uint32_t& value) {
    if (lua_type(L, index) != LUA_TNUMBER) return false;
    const double number = lua_tonumber(L, index);
    if (!std::isfinite(number) || number < 0 || number > 4294967295.0 || number != std::floor(number)) return false;
    value = static_cast<uint32_t>(number);
    return true;
}

int Set(lua_State* L) {
    uint32_t low = 0, high = 0, top = 0, bottom = 0;
    if (!g_enabled || !Number(L, 1, low) || !Number(L, 2, high) || !Number(L, 3, top) || !Number(L, 4, bottom) ||
        !wxl::underwear::Allowed(top, 0) || !wxl::underwear::Allowed(bottom, 1) || (!low && !high)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const uint64_t guid = (uint64_t(high) << 32) | low;
    auto& profiles = Profiles();
    auto found = profiles.find(guid);
    if (found == profiles.end()) {
        if ((!top && !bottom) || profiles.size() >= kMaxOwners) {
            lua_pushboolean(L, !top && !bottom);
            return 1;
        }
        found = profiles.try_emplace(guid).first;
    }

    Profile& profile = found->second;
    if (profile.top != top || profile.bottom != bottom) {
        profile.top = top;
        profile.bottom = bottom;
        profile.topPath = wxl::underwear::TexturePath(top, true);
        profile.bottomPath = wxl::underwear::TexturePath(bottom, false);
        ReleaseLower(profile);
        const std::string lowerPath = wxl::underwear::LowerTexturePath(top);
        if (!lowerPath.empty()) profile.lowerTexture = TextureCacheCreateTexture(lowerPath.c_str());
        profile.dirty = true;
    }

    Refresh(guid, profile);
    if (!top && !bottom) {
        ReleaseLower(profile);
        profiles.erase(found);
    }
    lua_pushboolean(L, 1);
    return 1;
}

int Reset(lua_State*) {
    auto& profiles = Profiles();
    for (auto& [guid, profile] : profiles) {
        profile.top = profile.bottom = 0;
        ReleaseLower(profile);
        profile.dirty = true;
        if (g_enabled) Refresh(guid, profile);
    }
    profiles.clear();
    return 0;
}

int Poll(lua_State*) {
    if (g_enabled) {
        for (auto& [guid, profile] : Profiles()) Refresh(guid, profile);
    }
    return 0;
}

int Ready(lua_State* L) {
    lua_pushboolean(L, g_enabled ? 1 : 0);
    return 1;
}

}  // namespace

void WxlRegisterUnderwearLua() {
    g_enabled = WxlProfileEnabled("WXL_UNDERWEAR", true);
    FrameScript_RegisterFunction("WXL_UnderwearSet", &Set);
    FrameScript_RegisterFunction("WXL_UnderwearReset", &Reset);
    FrameScript_RegisterFunction("WXL_UnderwearRefresh", &Poll);
    FrameScript_RegisterFunction("WXL_UnderwearReady", &Ready);
}

void WxlUnderwearWorldLeave() {
    // The characters are being torn down; only drop the references this module owns.
    auto& profiles = Profiles();
    for (auto& entry : profiles) ReleaseLower(entry.second);
    profiles.clear();
}

CharSectionsRec* WxlUnderwearRecord(const CCharacterComponent* component, CharSectionsRec* original) {
    auto& profiles = Profiles();
    if (!original || profiles.empty()) return original;
    // Called only when the engine looks up underwear, never once per draw or texture pixel.
    for (auto& [guid, profile] : profiles) {
        if ((!profile.top && !profile.bottom) || ComponentFor(guid) != component) continue;
        profile.record = *original;
        if (profile.bottom) profile.record.m_textureName[0] = profile.bottomPath.c_str();
        if (profile.top) profile.record.m_textureName[1] = profile.topPath.c_str();
        return &profile.record;
    }
    return original;
}

HTEXTURECACHE WxlUnderwearLowerTexture(const CCharacterComponent* component) {
    auto& profiles = Profiles();
    if (!g_enabled || profiles.empty()) return nullptr;
    // Match the upper-underwear visibility gate: a covered top adds no lower band.
    if (component->m_section[SECTION_TORSO_UPPER].m_layerMask & 7) return nullptr;
    for (auto& [guid, profile] : profiles) {
        if (profile.lowerTexture && ComponentFor(guid) == component) return profile.lowerTexture;
    }
    return nullptr;
}

#endif  // WOWDECOMP_LINUX
