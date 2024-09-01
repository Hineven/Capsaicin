// Shared structs among CPU and GPU
#include "../../gpu_shared.h"

// Parameters & Globals & Helper functions
#include "migi_inc.hlsl"

// Library for MIGI
#include "migi_lib.hlsl"

#include "migi_probes.hlsl"
#include "migi_worldcache.hlsl"

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

DebugIncidentRadiance_Output DebugSSRC_VisualizeProbeSGDirection (
    in uint VertexIndex : SV_VertexID, // Vertex index
    in uint InstanceID  : SV_InstanceID // Instance Index : probe index
) {
    int2 ProbeIndex = g_RWDebugProbeIndexBuffer[0];
    ProbeHeader Header = GetScreenProbeHeader(ProbeIndex);
    int BasisCount = GetProbeBasisCountFromClass(Header.Class);
    DebugIncidentRadiance_Output Output;
    if(InstanceID < BasisCount) {
        float3 World     = g_RWProbeWorldPositionTexture[ProbeIndex].xyz;
        SGData SG = FetchBasisData(Header.BasisOffset + InstanceID);
        if(VertexIndex == 1) {
            World += (dot(EvaluateSG(SG, SG.Direction), 1.f.xxx) + 0.05f) * SG.Direction;
        }
        Output.Position  = mul(MI.CameraProjView, float4(World, 1.f));
        Output.Color     = float4(BasisIndexToColor(InstanceID), 1.f);
    } else {
        Output.Color = float4(0.f, 0.f, 0.f, 0.f);
        Output.Position = float4(0.f, 0.f, 0.f, 1.f);
    }
    return Output;
}

struct DebugUpdateRays_Output {
    float4 Position : SV_Position;
    linear float4 Color    : COLOR;
};

DebugUpdateRays_Output DebugSSRC_VisualizeUpdateRays (
    in uint VertexIndex : SV_VertexID, // Vertex index
    in uint InstanceID  : SV_InstanceID // Instance Index : ray rank
) {
    int2 ProbeIndex = g_RWDebugProbeIndexBuffer[0];
    int  ProbeIndex1 = ProbeIndex.x + ProbeIndex.y * MI.TileDimensions.x;
    ProbeHeader Header = GetScreenProbeHeader(ProbeIndex);
    int RayRank   = InstanceID;
    int RayIndex  = g_RWProbeUpdateRayOffsetBuffer[ProbeIndex1] + RayRank;
    float3 RayOrigin         = Header.Position;
    float3 RayDirection      = OctahedronToUnitVector(unpackUnorm2x16(g_RWUpdateRayDirectionBuffer[RayIndex]) * 2.f - 1.f);
    float4 RayRadianceInvPdf = UnpackFp16x4(g_RWUpdateRayRadianceInvPdfBuffer[RayIndex]);
    float3 RayRadiance       = RayRadianceInvPdf.xyz;
    float  InvPdf            = RayRadianceInvPdf.w;
    // Negative depth indicate backface hits
    float  RayLinearDepth    = abs(g_RWUpdateRayLinearDepthBuffer[RayIndex]);
    float3 World;
    if(VertexIndex == 0) {
        World = RayOrigin;
    } else {
        World = RayOrigin + RayDirection * RayLinearDepth;
    }
    DebugUpdateRays_Output Output;
    Output.Position  = mul(MI.CameraProjView, float4(World, 1.f));
    float3 Color     = RayRadiance;
    Output.Color     = float4(Color, MI.DebugVisualizeChannel == 0 ? 1.f : (InvPdf > 0 ? 1.f : 0.f));
    if(MI.DebugVisualizeChannel == 2) {
        Output.Color.xyz = float3(VertexIndex == 1 ? 0 : 1, 0, VertexIndex == 1 ? 1 : 0);
    }
    return Output;
}


struct DebugLight_Output {
    float4 Position : SV_Position;
    float4 Color    : COLOR;
};

DebugLight_Output DebugSSRC_VisualizeLight (
    in uint VertexIndex : SV_VertexID // Vertex index
) {
    float3 Direction = FibonacciSphere(VertexIndex, 32768);
    float3 World     = MI.DebugLightPosition + Direction * MI.DebugLightSize;
    DebugLight_Output Output;
    Output.Position  = mul(MI.CameraProjView, float4(World, 1.f));
    float3 Color     = MI.DebugLightColor;
    Output.Color     = float4(Color, 1.f);
    return Output;
}

struct DebugWorldCache_Output {
    float4 Position : SV_Position;
    float4 ProbeDirection_ID : COLOR;
};

DebugWorldCache_Output DebugWorldCache_VisualizeProbes (
    // gfx reflects on vertex stride automatically
    in float3 LocalVertexPosition : SV_Position,
    in uint InstanceIndex  : SV_InstanceID
) {
    int ProbeIndex = g_RWWorldCacheActiveProbeIndexBuffer[InstanceIndex];
    float3 ProbeWorldPosition = WorldCache_GetProbeHeader(ProbeIndex).WorldPosition;
    float Scale = 0.015f;
    if(MI.DebugVisualizeChannel == 1) {
        float4 ScreenPosition_w = mul(MI.CameraProjView, float4(ProbeWorldPosition, 1.f));
        if(ScreenPosition_w.w > 0) {
            float ScreenDepth = ScreenPosition_w.z / ScreenPosition_w.w;
            float2 FilmUV = NDC22UV(ScreenPosition_w.xy / ScreenPosition_w.w);
            float FilmDepth = g_DepthTexture.SampleLevel(g_LinearSampler, FilmUV, 0).r;
            if(0 < ScreenDepth && ScreenDepth < min(1, FilmDepth) ) {
                int2 PixelCoords = FilmUV * float2(MI.ScreenDimensions);
                int  Dist = dot(abs(PixelCoords - int2(MI.DebugCursorPixelCoords)), 1);
                if(Dist < 10) {        
                    float2 Oct01 = UnitVectorToOctahedron01(normalize(LocalVertexPosition));
                    float2 SubTexPosition = 1 + Oct01 * WORLD_CACHE_PROBE_RESOLUTION_INTERNAL;
                    int2 ProbeAtlasBase = WorldCache_GetProbeAtlasBase(ProbeIndex);
                    float2 UV = (SubTexPosition + ProbeAtlasBase) * WorldCache.InvAtlasDimensions;
                    float2 Momentum = g_WorldCacheMomentumTexture.SampleLevel(g_LinearSampler, UV, 0);
                    Scale = Momentum.x * 0.5f;
                }
            }
        }
    }
    float3 VertexPosition = LocalVertexPosition * Scale + ProbeWorldPosition;
    DebugWorldCache_Output Output;
    Output.Position = mul(MI.CameraProjView, float4(VertexPosition, 1.f));
    Output.ProbeDirection_ID = float4(normalize(LocalVertexPosition), ProbeIndex);
    return Output;
}