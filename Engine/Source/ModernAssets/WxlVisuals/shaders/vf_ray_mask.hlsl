#include "vf_common.hlsli"

float4 cRayMask : register(c9);
float4 cRaySource : register(c10);

sampler2D sDepth : register(s0);
sampler2D sDownsampledScene : register(s1);

static const float3 kLumaWeights = float3(0.299, 0.587, 0.114);
static const float kThresholdRampSlope = 4;

float3 DirectionToSunView()
{
    return cRayMask.xyz;
}

float AngularFalloffExponent()
{
    return cRayMask.w;
}

float LuminanceThreshold()
{
    return cRaySource.y;
}

float2 RayTexelSize()
{
    return cRaySource.zw;
}

float SkyAt(float2 pixel)
{
    pixel = clamp(pixel, ViewportOrigin() + 0.5, ViewportLastPixelCentre());
    return IsSky(SampleDepth(sDepth, pixel)) ? 1 : 0;
}

float4 main(float2 rayTexel : VPOS) : COLOR0
{
    float2 fullPixelsPerRayTexel = ViewportSize() * RayTexelSize();
    float2 pixel = min(ViewportOrigin() + (rayTexel + 0.5) * fullPixelsPerRayTexel, ViewportLastPixelCentre());
    float skyMask;
    [branch] if (cRaySource.x > 0)
    {
        float2 radius = fullPixelsPerRayTexel * 0.25;
        skyMask = 0.25 * (SkyAt(pixel - radius) + SkyAt(pixel + radius) +
                          SkyAt(pixel + float2(-radius.x, radius.y)) +
                          SkyAt(pixel + float2(radius.x, -radius.y)));
    }
    else
        skyMask = IsSky(SampleDepth(sDepth, pixel)) ? 1 : 0;
    float3 viewDirection = normalize(ViewRayAtUnitDepth(pixel));
    float sunFalloff = pow(saturate(dot(viewDirection, DirectionToSunView())), AngularFalloffExponent());
    float3 scene = tex2Dlod(sDownsampledScene, float4((rayTexel + 0.5) * RayTexelSize(), 0, 0)).rgb;
    float luminance = dot(scene, kLumaWeights);
    float brightSky = skyMask * sunFalloff * saturate((luminance - LuminanceThreshold()) * kThresholdRampSlope);
    return float4(scene * brightSky, 1);
}
