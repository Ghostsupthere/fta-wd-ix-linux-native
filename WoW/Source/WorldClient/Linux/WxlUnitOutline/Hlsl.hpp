// wxl-unit-outline: a reaction-colored silhouette outline on the mouseover and target units.
// A render script: it owns its shaders and logic and draws through the core gx facade; it never
// touches an offset or installs a hook. It subscribes to render events and self-registers at load.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

namespace
{
    // Writes opaque model batches into the mask. Do not sample texture alpha here: many diffuse
    // alpha channels carry material/detail data and would turn the silhouette into internal noise.
    const char* kColorHLSL =
        "float4 c0 : register(c0);\n"
        "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
        "  return c0;\n"
        "}\n";

    // Writes alpha-tested batches into the mask, following real cutouts (wings, hair cards, etc.).
    const char* kCutoutColorHLSL =
        "sampler2D s0 : register(s0);\n"
        "float4 c0 : register(c0);\n"
        "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
        "  clip(tex2D(s0, uv).a - 0.5);\n"
        "  return c0;\n"
        "}\n";

    // 8-tap edge detect on the mask: a thin anti-aliased line outside the silhouette.
    const char* kEdgeHLSL =
        "sampler2D m : register(s0);\n"
        "float4 px : register(c0);\n"
        "float4 effect : register(c1);\n"
        "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
        "  float2 o = px.xy * px.z;\n"
        "  float4 a = tex2D(m,uv+float2(o.x,0)) + tex2D(m,uv+float2(-o.x,0))\n"
        "           + tex2D(m,uv+float2(0,o.y)) + tex2D(m,uv+float2(0,-o.y))\n"
        "           + tex2D(m,uv+float2(o.x,o.y)) + tex2D(m,uv+float2(-o.x,-o.y))\n"
        "           + tex2D(m,uv+float2(o.x,-o.y)) + tex2D(m,uv+float2(-o.x,o.y));\n"
        "  float inside = tex2D(m, uv).a;\n"
        "  float outline = saturate(a.a * 0.125 * 1.6) * (1.0 - inside);\n"
        "  clip(outline - 0.02);\n"
        "  float3 col = a.a > 0.001 ? a.rgb / a.a : float3(1,1,1);\n"
        "  return float4(col, saturate(outline * 1.4 * effect.x));\n"
        "}\n";

    // Alpha/luminance decal: c0 supplies the reaction tint while the generated texture carries the polished
    // rim/body/ridge artwork. Transparent texels are clipped so only the V writes blended color.
    const char* kChevronHLSL =
        "sampler2D decal : register(s0);\n"
        "float4 c0 : register(c0);\n"
        "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
        "  float4 t = tex2D(decal, uv);\n"
        "  clip(t.a - 0.01);\n"
        "  float shade = lerp(0.16, 1.18, t.r);\n"
        "  float ridge = saturate((t.r - 0.88) * 8.0);\n"
        "  float3 col = lerp(c0.rgb * shade, float3(1,1,1), ridge * 0.32);\n"
        "  return float4(saturate(col), c0.a * t.a);\n"
        "}\n";

    // The exact solid-color shader used by the prior always-visible three-layer overlay.
    const char* kOverlayChevronHLSL =
        "float4 c0 : register(c0);\n"
        "float4 main() : COLOR0 { return c0; }\n";
}


