// Shared structs among CPU and GPU
#include "../../gpu_shared.h"

// Parameters & Globals & Helper functions
#include "migi_inc.hlsl"

// Library for MIGI
#include "migi_lib.hlsl"

struct DebugIncidentRadiance_Output {
    float4 Position : SV_Position;
    float4 Color    : COLOR;
};

DebugIncidentRadiance_Output DebugSSRC_VisualizeIncidentRadiance (
    in uint VertexIndex : SV_VertexID // Vertex index
) {
    float3 Origin    = g_RWDebugProbeWorldPositionBuffer[0];
    int    Index     = VertexIndex;
    float3 Direction = FibonacciSphere(Index, MI.DebugVisualizeIncidentRadianceNumPoints);
    float3 Radiance  = g_RWDebugVisualizeIncidentRadianceBuffer[Index];
    float3 World     = Origin + Direction * (dot(Radiance, 1.f.xxx) * 0.1f);
    
    DebugIncidentRadiance_Output Output;
    Output.Position  = mul(MI.CameraProjView, float4(World, 1.f));
    Output.Color     = float4(Radiance, 1.f);
    return Output;
}

// struct DebugUpdateRays_Output {
//     float4 Position : SV_Position;
//     float4 Color    : COLOR;
// };

// DebugUpdateRays_Output DebugSSRC_UpdateRays (
//     in uint VertexIndex : SV_VertexID, // Vertex index
//     in uint InstanceID  : SV_InstanceID // Instance Index : ray rank
// ) {
//     int2   TexCoords      = g_DebugCursorPixelCoords;
//     int2   TileCoords     = int2(TexCoords.x / SSRC_TILE_SIZE, TexCoords.y / SSRC_TILE_SIZE);
//     int    TileID         = TileCoords.x + TileCoords.y * g_TileDimensions.x;
//     int RayRank   = InstanceID;
//     int RayOffset = g_RWTileRayOffsetBuffer[TileID];
//     int RayCount  = g_RWTileRayCountBuffer[TileID];
//     int2 RayOriginCoords    = UnpackUint16x2(g_RWUpdateRayOriginBuffer[RayOffset + RayRank]);
//     float Depth             = g_DepthTexture.Load(int3(RayOriginCoords, 0)).x;
//     float2 UV               = (float2(RayOriginCoords) + 0.5f) * g_OutputDimensionsInv;
//     float3 RayOrigin        = InverseProject(g_CameraProjViewInv, UV, Depth);
//     float3 RayDirection     = UnpackNormal(g_RWUpdateRayDirectionBuffer[RayOffset + RayRank]);
//     float3 RayRadiance;
//     if(g_DebugVisualizeMode == 0) RayRadiance = UnpackFp16x3(g_RWUpdateRayRadianceInvPdfBuffer[RayOffset + RayRank]);
//     else if(g_DebugVisualizeMode == 1) RayRadiance = UnpackFp16x3(g_RWUpdateRayCacheBuffer[RayOffset + RayRank]);
//     // else if(g_DebugVisualizeMode == 2) {
//     //     float4 RealRayRadiancePdf = UnpackFp16x4(g_RWUpdateRayRadiancePdfBuffer[RayOffset + RayRank]);
//     //     float3 RealRayEval        = UnpackFp16x3(g_RWUpdateRayCacheBuffer[RayOffset + RayRank]);
//     //     RayRadiance = RealRayRadiancePdf.xyz - RealRayEval;
//     // }
//     float3 World;
//     if(VertexIndex == 0) {
//         World = RayOrigin;
//     } else {
//         World = RayOrigin + RayDirection * (dot(RayRadiance, 1.f.xxx) * 0.02f + 0.01f);
//     }
//     DebugUpdateRays_Output Output;
//     Output.Position  = mul(g_CameraProjView, float4(World, 1.f));
//     float3 Color     = 0.5f * (RayDirection + 1.f);
//     Output.Color     = float4(Color, 1.f);
//     return Output;
// }


// struct DebugLight_Output {
//     float4 Position : SV_Position;
//     float4 Color    : COLOR;
// };

// DebugLight_Output DebugSSRC_Light (
//     in uint VertexIndex : SV_VertexID // Vertex index
// ) {
//     float3 Direction = FibonacciSphere(VertexIndex, 32768);
//     float3 World     = g_DebugLightPosition + Direction * g_DebugLightSize;
//     DebugLight_Output Output;
//     Output.Position  = mul(g_CameraProjView, float4(World, 1.f));
//     float3 Color     = g_DebugLightColor;
//     Output.Color     = float4(Color, 1.f);
//     return Output;
// }