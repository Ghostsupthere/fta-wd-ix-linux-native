#include "vf_night_darkening.hlsli"

sampler2D Scene : register(s0);
sampler2D Halo : register(s1);
float4 Viewport : register(c0);
float2 Strength : register(c1);

float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 uv = (pixel + 0.5 - Viewport.xy) / Viewport.zw;
    float3 colour = DarkenNight(tex2Dlod(Scene, float4(uv, 0, 0)).rgb, Strength.x);

    [branch] if (Strength.y > 0.0)
    {
        float3 glow = tex2Dlod(Halo, float4(uv, 0, 0)).rgb * (0.65 * Strength.y);
        // Apply the same scale to all RGB channels so coloured lights keep their hue.
        float room = max(1.0 - PeakChannel(colour), 0.0);
        colour += glow * (room / max(room + PeakChannel(glow), 1e-4));
    }
    return float4(saturate(colour), 1.0);
}
