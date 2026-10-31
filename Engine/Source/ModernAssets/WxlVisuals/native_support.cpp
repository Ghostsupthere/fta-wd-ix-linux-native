#include "engine_actors.h"
#include "engine_lights.h"
#include <algorithm>
float WaterContactDepth(const WaterContact& c) { return c.surface-c.position[2]; }
float ClientRippleDepthLimit(float height) { return std::max(1.0f,2.0f*height); }
namespace engine {
const char* LocalLightCaptureName(LocalLightCapture c) {
    switch(c) {
    case LocalLightCapture::Captured:return "captured";
    case LocalLightCapture::InteriorRejected:return "interior rejected";
    case LocalLightCapture::DisabledLight:return "disabled";
    default:return "unavailable";
    }
}
}
