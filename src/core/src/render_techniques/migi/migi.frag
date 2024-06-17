// Shared structs among CPU and GPU
#include "../../gpu_shared.h"

// Parameters & Globals & Helper functions
#include "migi_inc.hlsl"

// Library for MIGI
#include "migi_lib.hlsl"


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