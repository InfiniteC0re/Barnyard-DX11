// Static per-material constants (b5), shared by World and Skin. One immutable 256-byte page per
// XML material record, selected per draw via a *SetConstantBuffers1 offset -- never uploaded at
// draw time. Value clamps are baked into the records by BuildMaterialConstantsBuffer
cbuffer MaterialCB : register(b5)
{
    float4 mat_Reflectivity; // x = SSR reflectivity, y = fresnel power, z = specular intensity, w = specular power
    float4 mat_MapParams;    // x = normal-map strength, y = roughness-map strength, z = parallax scale, w = roughness
    float4 mat_Params2;      // x = metallic, y = per-material IBL strength, z = F0, w = emissive intensity
    float4 mat_Wind;         // x = windMin, y = windMax, z = map flags (1 = normal, 2 = rough, 4 = metal)
};
