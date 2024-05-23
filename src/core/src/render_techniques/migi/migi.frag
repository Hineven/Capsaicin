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

// struct DebugUpdateRays_Input {
//     float4 Position : SV_Position;
//     float4 Color    : COLOR;
// };

// float4 DebugSSRC_UpdateRays (
//     in DebugUpdateRays_Input Input
// ) : SV_Target {
//     return Input.Color;
// }

struct DebugLight_Input {
    float4 Position : SV_Position;
    float4 Color    : COLOR;
};

float4 DebugSSRC_VisualizeLight (
    in DebugLight_Input Input
) : SV_Target {
    return Input.Color;
}