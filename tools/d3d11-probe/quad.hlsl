struct Vertex {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Vertex QuadVertex(uint id : SV_VertexID) {
    Vertex output;
    output.uv = float2(id & 1, id >> 1);
    output.position = float4(output.uv * float2(1, -1) + float2(-0.5, 0.5), 0, 1);
    return output;
}

Texture2D scene : register(t0);
SamplerState nearest : register(s0);

float4 QuadPixel(Vertex input) : SV_Target {
    return scene.Sample(nearest, input.uv);
}

#if PROBE_VERTEX
Vertex main(uint id : SV_VertexID) { return QuadVertex(id); }
#else
float4 main(Vertex input) : SV_Target { return QuadPixel(input); }
#endif
