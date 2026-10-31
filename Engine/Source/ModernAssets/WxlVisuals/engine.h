#pragma once

#include <d3d9.h>

#include "fog_data.h"
#include "engine_actors.h"
#include "engine_lights.h"
#include "water_types.h"

#include <cstdint>

struct FrameInputs
{
    float cameraRelativeView[16];
    float glProjection[16];
    float camPos[3];
    float camTarget[3];
    D3DVIEWPORT9 viewport;
    float dayFraction;
    float toLight[3];
    bool lightIsMoon;
    uint32_t fogColor;
    uint32_t sunColor;
    uint32_t directColor;
    uint32_t ambientColor;
    float fogStart;
    float fogEnd;
    float zoneFogDistance;
    float farClip;
    float clientGlowAmount;
    bool inLiquid;
    int mapId;
    LightParamsSelection lightParams;
    LocalLightInputs localLights;
};

constexpr int kSkyColorCount = 6;
constexpr int kWaterColorPair = 2;

struct WaterInputs
{
    uint32_t skyColors[kSkyColorCount];
    uint32_t riverColors[kWaterColorPair];
    uint32_t oceanColors[kWaterColorPair];
    bool stockFogApplies;
    WaterContactFrame contacts;
};

