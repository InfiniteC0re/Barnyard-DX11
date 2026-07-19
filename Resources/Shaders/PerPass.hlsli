// Per-pass constants (b4), shared by World/Skin/Grass. Uploaded lazily by
// RenderDX11::FlushConstantBuffers only when a value changed (pass boundaries), not per draw
cbuffer PerPassCB : register(b4)
{
    float4 pp_CameraPos;    // xyz = camera world position
    float4 pp_SunDirection; // xyz = direction toward the sun (world, CSM mapping)
    float4 pp_FogColor;     // xyz = fog colour, w = fog density
    float4 pp_FogParams;    // x = fog distance start, y = fog distance end
    float4 pp_WindParams;   // xy = wind direction (world XZ), z = strength, w = time (phase)
    float4 pp_EnvSpecular;  // x = global env-specular intensity (0 while capturing), y = cube max mip,
                            // z = reflection-capture active (1 = masking metallic/parallax), w = tangent-debug view
    float4 pp_EnvParallax;  // xyz = "to" box half-extents (world units)
    float4 pp_EnvProbePos;  // xyz = "to" probe centre, w = cross-fade blend (1 = fully "to")
    float4 pp_EnvParallax2; // xyz = "from" box half-extents
    float4 pp_EnvProbePos2; // xyz = "from" probe centre (outgoing cube during a switch)
    float4 pp_AmbientColor; // world ambient colour (World non-FOB + Grass vertex lighting)
    float4 pp_ShadowColor;  // world shadow colour (World + Grass vertex lighting)
    float4x4 pp_matViewProj; // world -> clip; per-draw b0 carries only the model matrix
};
