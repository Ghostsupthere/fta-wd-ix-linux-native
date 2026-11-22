float PeakChannel(float3 colour)
{
    return max(colour.r, max(colour.g, colour.b));
}

float3 DarkenNight(float3 colour, float darkness)
{
    return colour * (1.0 - darkness * (1.0 - smoothstep(0.30, 0.95, PeakChannel(colour))));
}
