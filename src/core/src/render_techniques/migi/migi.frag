// Shared structs among CPU and GPU
#include "../../gpu_shared.h"

// Parameters & Globals & Helper functions
#include "migi_inc.hlsl"

// Library for MIGI
#include "migi_lib.hlsl"
#include "migi_worldcache.hlsl"

struct DebugIncidentRadiance_Input {
    float4 Position : SV_Position;
    float4 Color    : COLOR;
};

float4 DebugSSRC_VisualizeIncidentRadiance (
    in DebugIncidentRadiance_Input Input
) : SV_Target {
    return Input.Color;
}

float4 DebugSSRC_VisualizeProbeSGDirection (
    in DebugIncidentRadiance_Input Input
) : SV_Target {
    if(Input.Color.a == 0) discard;
    return Input.Color;
}

struct DebugUpdateRays_Input {
    float4 Position : SV_Position;
    linear float4 Color    : COLOR;
};

float4 DebugSSRC_VisualizeUpdateRays (
    in DebugUpdateRays_Input Input
) : SV_Target {
    if(Input.Color.w == 0) discard;
    if(MI.DebugVisualizeChannel == 2 && Input.Color.z > 0.95f) Input.Color.xyz = float3(0.f, 1.f, 0.f);
    return Input.Color;
}

struct DebugLight_Input {
    float4 Position : SV_Position;
    float4 Color    : COLOR;
};

float4 DebugSSRC_VisualizeLight (
    in DebugLight_Input Input
) : SV_Target {
    return Input.Color;
}


struct DebugWorldCache_Input {
    float4 Position : SV_Position;
    float4 ProbeDirection_ID : COLOR;
};

float4 DebugWorldCache_VisualizeProbes (
    in DebugWorldCache_Input Input
) : SV_Target {
    float3 Direction = normalize(Input.ProbeDirection_ID.xyz);
    int ProbeID = round(Input.ProbeDirection_ID.w);
    int2 ProbeAtlasBase = WorldCache_GetProbeAtlasBase(ProbeID);
    float2 Oct01 = UnitVectorToOctahedron01(Direction);
    float2 ProbeAtlasUV = (ProbeAtlasBase + 1 + Oct01 * WORLD_CACHE_PROBE_RESOLUTION_INTERNAL) * WorldCache.InvAtlasDimensions;
    float3 Color = 0;
    if(MI.DebugVisualizeChannel == 0) {
        Color = g_WorldCacheIrradiance2PLuminanceTexture.SampleLevel(g_LinearSampler, ProbeAtlasUV, 0).xyz;
    } else {
        Color.r = g_WorldCacheMomentumTexture.SampleLevel(g_LinearSampler, ProbeAtlasUV, 0).y;
    }
    return float4(Color, 1);
}


struct DebugSSRC_ProbeInput {
    float4 Position : SV_Position;
    float4 Normal   : COLOR;
};

float4 DebugSSRC_VisualizeProbe (
    in DebugSSRC_ProbeInput Input
) : SV_Target {
    float3 LightDir = normalize(float3(0.5f, 1.f, 0.5f));
    float3 Albedo = 0.5f.xxx;
    float3 Normal = Input.Normal.xyz;
    float3 Color  = Albedo * (0.1f + saturate(dot(Normal, LightDir)) * 0.9f);
    return float4(Color, 1);
}

struct DebugSSRC_ProbeRaysInput {
    float4 Position : SV_Position;
    float4 Color    : COLOR;
};

float4 DebugSSRC_VisualizeProbeRays (
    in DebugSSRC_ProbeRaysInput Input
) : SV_Target {
    return Input.Color;
}