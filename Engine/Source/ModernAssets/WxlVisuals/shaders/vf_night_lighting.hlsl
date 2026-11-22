sampler2D Scene : register(s0);
float4 Viewport : register(c0);
float2 Strength : register(c1);

float3 Bright(float2 uv)
{
    return max(tex2Dlod(Scene, float4(uv, 0, 0)).rgb - 0.72, 0.0);
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 uv = (pixel + 0.5 - Viewport.xy) / Viewport.zw;
    float3 colour = tex2Dlod(Scene, float4(uv, 0, 0)).rgb;
    float peak = max(colour.r, max(colour.g, colour.b));
    colour *= 1.0 - Strength.x * (1.0 - smoothstep(0.30, 0.95, peak));

    if (Strength.y > 0.0)
    {
        float2 stepUV = 4.0 / Viewport.zw;
        float3 halo = Bright(uv + float2(stepUV.x, 0)) + Bright(uv - float2(stepUV.x, 0))
                    + Bright(uv + float2(0, stepUV.y)) + Bright(uv - float2(0, stepUV.y));
        colour += (0.25 * Strength.y) * halo;
    }
    return float4(saturate(colour), 1.0);
}
