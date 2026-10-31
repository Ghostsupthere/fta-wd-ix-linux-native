#pragma once

#include "config.h"

#include <string>

struct VisualSelection
{
    bool enabled = true;
    bool water = true;
    bool multisampling = true;
    bool densityVariation = true;
    bool distanceFog = true;
    bool distanceHaze = true;
    bool forwardScattering = true;
    bool localLights = false;
    bool interiorTransitions = true;
    bool godRays = false;
    bool groundMist = false;
    bool transparentFog = false;
    bool glowCompensation = true;
    bool extendedViewDistance = true;
    bool waterRipples = true;
    float fogDensityScale = 0.5f;
};

VisualSelection ReadVisualSelection(const std::string& clientConfigPath, const Config& fallback);
void ApplyVisualSelection(Config& config, const VisualSelection& selection);
void ApplyRuntimeVisualSelection(Config& config);
bool VisualFogEnabled(const Config& config);
