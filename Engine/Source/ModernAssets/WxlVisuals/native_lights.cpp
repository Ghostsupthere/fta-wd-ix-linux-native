#include "engine_lights.h"
#include <algorithm>
#include <cmath>
namespace {
constexpr float kMaxWorldCoordinate=1000000.0f,kMaxLightComponent=10000.0f,kMaxUploadedLightComponent=1000000.0f,kMaxAttenuationCoefficient=1000000.0f;
bool ValidVector(const float values[3], float minimum, float maximum)
{
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(values[i]) || values[i] < minimum || values[i] > maximum)
            return false;
    return true;
}

double SquaredDistance(const float a[3], const float b[3])
{
    double result = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        const double delta = static_cast<double>(a[i]) - b[i];
        result += delta * delta;
    }
    return result;
}

float PeakColor(const float color[3])
{
    return std::max(color[0], std::max(color[1], color[2]));
}

float LinearChannel(float channel, float peak)
{
    const float gammaChannel = std::max(channel, 0.0f);
    if (peak <= 1.0f)
        return std::pow(gammaChannel, kLocalLightGamma);
    return std::pow(gammaChannel / peak, kLocalLightGamma) * peak;
}

double LightPriority(const LocalPointLight& light, const float cameraPosition[3])
{
    const double distanceSquared = SquaredDistance(light.position, cameraPosition);
    const double denominator = light.attenuation[0] + light.attenuation[1] * std::sqrt(distanceSquared) +
                               light.attenuation[2] * distanceSquared;
    return PeakColor(light.uploadedColor) / std::max(denominator, 1.0);
}

bool Precedes(const LocalPointLight& a, const LocalPointLight& b, const float cameraPosition[3])
{
    const double aPriority = LightPriority(a, cameraPosition);
    const double bPriority = LightPriority(b, cameraPosition);
    if (aPriority != bPriority)
        return aPriority > bPriority;
    for (int i = 0; i < 3; ++i)
        if (a.position[i] != b.position[i])
            return a.position[i] < b.position[i];
    for (int i = 0; i < 3; ++i)
        if (a.color[i] != b.color[i])
            return a.color[i] > b.color[i];
    for (int i = 0; i < 3; ++i)
        if (a.attenuation[i] != b.attenuation[i])
            return a.attenuation[i] < b.attenuation[i];
    return false;
}

}
namespace engine {
void UploadedPointLightColor(const float captured[3], const PointLightUpload& upload, float uploaded[3])
{
    const float peak = PeakColor(captured);
    for (int channel = 0; channel < 3; ++channel)
        uploaded[channel] = (upload.linear ? LinearChannel(captured[channel], peak)
                                           : std::max(captured[channel], 0.0f)) * upload.intensity;
}

float PointLightCutoff(const float color[3], const float attenuation[3])
{
    if (!ValidVector(color, 0.0f, kMaxUploadedLightComponent) ||
        !ValidVector(attenuation, 0.0f, kMaxAttenuationCoefficient))
        return 0.0f;
    const double peak = PeakColor(color);
    const double target = peak / kLocalPointLightContributionCutoff;
    if (peak <= 0.0 || target <= std::max(static_cast<double>(attenuation[0]), 1.0))
        return 0.0f;
    const double remaining = target - attenuation[0];
    double radius = kMaxLocalPointLightRadius;
    if (attenuation[2] > 0.0f)
    {
        const double linear = attenuation[1];
        radius = 2.0 * remaining /
                 (linear + std::sqrt(linear * linear + 4.0 * attenuation[2] * remaining));
    }
    else if (attenuation[1] > 0.0f)
        radius = remaining / attenuation[1];
    return static_cast<float>(std::min(radius, static_cast<double>(kMaxLocalPointLightRadius)));
}

bool SelectLocalPointLight(LocalLightInputs& out, const LocalPointLight& light, const float cameraPosition[3],
                           const PointLightUpload& upload)
{
    if (out.pointLightCount > kMaxLocalPointLights ||
        !ValidVector(cameraPosition, -kMaxWorldCoordinate, kMaxWorldCoordinate) ||
        !ValidVector(light.position, -kMaxWorldCoordinate, kMaxWorldCoordinate) ||
        !ValidVector(light.color, 0.0f, kMaxLightComponent) || !std::isfinite(upload.intensity) ||
        upload.intensity < 0.0f)
        return false;
    LocalPointLight candidate = light;
    UploadedPointLightColor(candidate.color, upload, candidate.uploadedColor);
    candidate.cutoff = PointLightCutoff(candidate.uploadedColor, candidate.attenuation);
    const double limit = candidate.cutoff + kMaxLocalPointLightRadius;
    if (candidate.cutoff <= 0.0f || SquaredDistance(candidate.position, cameraPosition) > limit * limit)
        return false;
    uint32_t index = 0;
    while (index < out.pointLightCount && !Precedes(candidate, out.pointLights[index], cameraPosition))
        ++index;
    if (index == kMaxLocalPointLights)
        return false;
    const uint32_t count = std::min(out.pointLightCount + 1, kMaxLocalPointLights);
    for (uint32_t i = count - 1; i > index; --i)
        out.pointLights[i] = out.pointLights[i - 1];
    out.pointLights[index] = candidate;
    out.pointLightCount = count;
    return true;
}

}
