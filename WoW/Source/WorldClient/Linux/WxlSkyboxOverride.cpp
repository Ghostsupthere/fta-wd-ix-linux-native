#include "WoW/Source/WorldClient/Linux/WxlSkyboxOverride.h"

#if defined(WOWDECOMP_LINUX)

#include <cmath>
#include <cstdint>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/LightSkyboxRec.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"

namespace {

uint32_t s_overrideId = 0;

uint32_t NormalizeSkyboxId(double value) {
    if (!std::isfinite(value) || value <= 0.0) {
        return 0;
    }

    if (value >= 65535.0) {
        return 65535;
    }

    return static_cast<uint32_t>(value + 0.5);
}

int32_t Script_WarcraftXL_SetSkyboxOverride(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }

    uint32_t id = NormalizeSkyboxId(lua_tonumber(L, 1));
    const LightSkyboxRec* record = id ? g_lightSkyboxDB.GetRecord(static_cast<int32_t>(id)) : nullptr;

    if (id && !record) {
        lua_pushboolean(L, 0);
        lua_pushnumber(L, static_cast<double>(s_overrideId));
        return 2;
    }

    s_overrideId = id;

    lua_pushboolean(L, 1);
    lua_pushnumber(L, static_cast<double>(id));
    return 2;
}

int32_t Script_WarcraftXL_GetSkyboxOverride(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(s_overrideId));
    return 1;
}

}  // namespace

void WxlApplySkyboxOverride(DayNight::DNInfo& info) {
    if (!s_overrideId) {
        return;
    }

    const LightSkyboxRec* skybox = g_lightSkyboxDB.GetRecord(static_cast<int32_t>(s_overrideId));

    if (!skybox) {
        return;
    }

    // Mirrors wxl-skybox-control's ApplyForcedSkybox(): make the requested
    // model the fully opaque primary sky layer. DayNight's own draw path
    // (DrawSkyObjects) already skips every blend layer once m_skyboxWeight
    // reaches 1.0 (see DayNight.cpp's "if (!info.m_skybox || info.m_skyboxWeight
    // < 1.0f)" guard), so clearing the blend array here is belt-and-braces,
    // not strictly required -- but matches the original's explicit
    // setSlot(1, nullptr, ...)/setSlot(2, nullptr, ...) intent.
    info.m_skybox = DayNight::GetOverrideSky(skybox->m_name, skybox->m_flags);
    info.m_skyboxWeight = 1.0f;

    for (int32_t i = 0; i < 3; i++) {
        info.m_blendSkybox[i] = nullptr;
        info.m_blendSkyboxWeight[i] = 0.0f;
        info.m_blendSkyboxFlags[i] = 0;
    }
}

void WxlRegisterSkyboxLua() {
    FrameScript_RegisterFunction("WarcraftXL_SetSkyboxOverride", &Script_WarcraftXL_SetSkyboxOverride);
    FrameScript_RegisterFunction("WarcraftXL_GetSkyboxOverride", &Script_WarcraftXL_GetSkyboxOverride);
}

#endif  // WOWDECOMP_LINUX
