#if defined(WOWDECOMP_LINUX)
#include "WxlGrassMotion.h"
#include "WxlGrassInstancing.h"
#include "WxlVisualEnvironment.h"
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include "Engine/Source/ModernAssets/WxlGrass/GrassShaderText.hpp"
#include "Engine/Source/ModernAssets/WxlGrass/GrassRigid.hpp"
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/WorldClient/World.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {
using wxl::grass::PhysicsSettings;
using wxl::grass::WindSettings;

// Active rows: the original's compile-time defaults.
const WindSettings wind = {true, 45.0f, 6.0f, 0.12f, 18.0f, 0.05f, 6.5f, 35.0f, 0.35f, 0.6f, 0.015f, 0.3f};
const PhysicsSettings physics = {true, 2.0f, 0.5f, 0.0f, 0.5f, 0.3f};
constexpr float kEps = 1.0e-3f, kJitter = 1.7f, kTwoPi = 6.2831853f;

wxl::wmo::ShaderPack pack;
bool initialised = false, packLoaded = false, enabled = false;
unsigned patchedShaders = 0;
std::atomic<bool> swayDisabled{false};
bool frameDirty = true;
float cached[36] = {};

bool Replace(const uint8_t* code, size_t size, const uint32_t** out, size_t* outBytes) {
    const wxl::wmo::PackEntry* entry = pack.FindVertex(code, size);
    if (!entry) return false;
    *out = entry->bytecode.data(); *outBytes = entry->bytecode.size() * 4;
    ++patchedShaders;
    return true;
}

// Port of the original per-frame constant block (c14..c22).
void Compute() {
    const C3Vector* camera = World::GetCamPos();
    float player[3] = {0, 0, 0};
    bool playerValid = false;
    if (CGPlayer_C* active = ClntObjMgrGetActivePlayerObj()) {
        const C3Vector position = active->GetPosition();
        player[0] = position.x; player[1] = position.y; player[2] = position.z;
        playerValid = std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
    }
    const WindSettings& w = wind; const PhysicsSettings& p = physics;
    const bool allowed = !swayDisabled.load(std::memory_order_relaxed) && camera && playerValid;
    const bool windOn = allowed && w.enabled, physicsOn = allowed && p.enabled;
    const float t = float(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() % 1000000) * 0.001f;
    const float a1 = w.directionDeg * (kTwoPi / 360.0f), a2 = (w.directionDeg + w.crossAngleDeg) * (kTwoPi / 360.0f);
    const float k1 = kTwoPi / (w.wavelength > 0.1f ? w.wavelength : 0.1f), k2 = kTwoPi / (w.crossWavelength > 0.1f ? w.crossWavelength : 0.1f);
    const float d1[2] = {std::cos(a1), std::sin(a1)}, d2[2] = {std::cos(a2), std::sin(a2)};
    const float amp1 = windOn ? w.amplitude : 0.0f, amp2 = windOn ? w.crossAmplitude : 0.0f, lean = windOn ? w.lean : 0.0f;
    const float phase1 = -std::fmod(k1 * w.speed * t, kTwoPi), phase2 = -std::fmod(k2 * w.speed * 1.37f * t, kTwoPi);
    const float center = physicsOn ? p.forceCenter : 0.0f, edge = physicsOn ? p.forceEdge : 0.0f;
    const float coneHeight = p.coneHeight > 0.01f ? p.coneHeight : 0.01f, radius = p.radius > 0.01f ? p.radius : 0.01f;
    float variance = w.variance; variance = variance < 0 ? 0 : (variance > 1 ? 1 : variance);
    float anchor = w.anchor; anchor = anchor < 0 ? 0 : (anchor > 0.9f ? 0.9f : anchor);
    const float cam[3] = {camera ? camera->x : 0.0f, camera ? camera->y : 0.0f, camera ? camera->z : 0.0f};
    const float block[9][4] = {
        {cam[0], cam[1], cam[2], phase2},                                           // c14
        {1.0f / coneHeight, 1.0f / radius, p.minHeight, kEps},                      // c15
        {player[0], player[1], player[2], lean},                                    // c16
        {edge - center, center, radius, phase1},                                    // c17
        {d1[0] * amp1, d1[1] * amp1, d1[0] * k1, d1[1] * k1},                       // c18
        {d2[0] * amp2, d2[1] * amp2, d2[0] * k2, d2[1] * k2},                       // c19
        {0.737f, 1.311f, kJitter, w.distanceFade > 0.0f ? w.distanceFade : 0.0f},   // c20
        {1.0f - variance * 0.5f, variance, -anchor, 1.0f / (1.0f - anchor)},        // c21
        {(windOn || physicsOn) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f},                    // c22
    };
    std::memcpy(cached, block, sizeof(cached));
}

struct RigidEntry { uint32_t id = 0; const char* path = nullptr; bool rigid = false; bool valid = false; };
RigidEntry rigidCache[1024];
}

void WxlGrassInitialize() {
    if (initialised) return;
    initialised = true;
    enabled = WxlProfileEnabled("WXL_GRASS_MOTION", true);
    if (!enabled) return;
    std::error_code error;
    const auto executable = std::filesystem::canonical("/proc/self/exe", error);
    const auto directory = error ? std::filesystem::current_path() : executable.parent_path();
    std::string why;
    packLoaded = pack.Load((directory / "Utils/WarcraftXL/visuals/grass_shaders.pack").string(), why);
    if (!packLoaded) {
        std::fprintf(stderr, "WarcraftXL: grass motion pack unavailable (%s); stock grass. Run tools/build-wmo-shader-pack.py\n", why.c_str());
        return;
    }
    NativeVisualHooks::replaceShaderCode = &Replace;
}

bool WxlGrassMotionActive() { return packLoaded && enabled && patchedShaders > 0; }

void WxlGrassFillConstants(float* block36) {
    if (!WxlGrassMotionActive()) return;
    static bool announced = false;
    if (!announced) {
        announced = true;
        std::fprintf(stderr, "WarcraftXL: grass motion active (%u patched shader(s))\n", patchedShaders);
    }
    if (frameDirty) { Compute(); frameDirty = false; }
    std::memcpy(block36, cached, sizeof(cached));
}

void WxlGrassFrame() { frameDirty = true; WxlGrassInstancingFrame(); }

bool WxlGrassIsRigid(uint32_t doodadId, const char* modelPath) {
    if (!modelPath || !*modelPath) return false;
    RigidEntry& entry = rigidCache[doodadId & 1023];
    if (entry.valid && entry.id == doodadId && entry.path == modelPath) return entry.rigid;
    entry.id = doodadId; entry.path = modelPath; entry.rigid = wxl::grass::IsRigidDetailDoodadName(modelPath); entry.valid = true;
    return entry.rigid;
}

void WxlGrassSetSwayDisabled(bool disabled) { swayDisabled.store(disabled, std::memory_order_release); frameDirty = true; }
bool WxlGrassSwayDisabled() { return swayDisabled.load(std::memory_order_acquire); }
#endif
