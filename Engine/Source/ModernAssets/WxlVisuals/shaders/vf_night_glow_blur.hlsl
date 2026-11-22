sampler2D Source : register(s0);
float4 TexelSizeAndStep : register(c0);

float3 SampleGlow(float2 uv)
{
    return tex2Dlod(Source, float4(uv, 0, 0)).rgb;
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 uv = (pixel + 0.5) * TexelSizeAndStep.xy;
    float2 step = TexelSizeAndStep.zw;
    float3 glow = 0.375 * SampleGlow(uv)
                + 0.250 * (SampleGlow(uv - step) + SampleGlow(uv + step))
                + 0.0625 * (SampleGlow(uv - 2 * step) + SampleGlow(uv + 2 * step));
    return float4(glow, 1);
}
