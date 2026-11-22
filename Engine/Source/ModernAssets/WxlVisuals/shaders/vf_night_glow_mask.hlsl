#include "vf_night_darkening.hlsli"

sampler2D Scene : register(s0);
sampler2D WorldDepth : register(s1);
float4 Viewport : register(c0); // origin and size in pixels
float4 DepthAndDarkness : register(c1); // depth texel size, sky cutoff, darkness
float2 GlowTexelSize : register(c2);

float3 ExtractGlow(float2 uv)
{
    float2 pixel = Viewport.xy + uv * Viewport.zw;
    float depth = tex2Dlod(WorldDepth, float4(pixel * DepthAndDarkness.xy, 0, 0)).r;
    [branch] if (depth >= DepthAndDarkness.z)
        return 0;

    float3 colour = DarkenNight(tex2Dlod(Scene, float4(uv, 0, 0)).rgb, DepthAndDarkness.w);
    float luma = dot(colour, float3(0.299, 0.587, 0.114));
    float brightness = max(luma, 0.65 * PeakChannel(colour));
    return colour * smoothstep(0.36, 0.72, brightness);
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 uv = (pixel + 0.5) * GlowTexelSize;
    float2 offset = GlowTexelSize * 0.25;
    float3 glow = ExtractGlow(uv + float2(-offset.x, -offset.y))
                + ExtractGlow(uv + float2( offset.x, -offset.y))
                + ExtractGlow(uv + float2(-offset.x,  offset.y))
                + ExtractGlow(uv + float2( offset.x,  offset.y));
    return float4(glow * 0.25, 1);
}
