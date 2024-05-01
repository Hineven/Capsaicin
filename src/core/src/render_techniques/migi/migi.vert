// Shared structs among CPU and GPU
#include "../../gpu_shared.h"

// Parameters & Globals & Helper functions
#include "migi_inc.hlsl"

// Library for MIGI
#include "migi_lib.hlsl"

struct InjectReprojectedBasis_Output
{
    float4 Position                   : SV_Position;
    nointerpolation float4 PARAMS     : TEXCOORD0;
    float4 PARAMS1                    : TEXCOORD1;
};


InjectReprojectedBasis_Output SSRC_InjectReprojectedBasis (
    in uint VertexIndex : SV_VertexID, // Vertex index
    in uint InstanceID  : SV_InstanceID // Instance Index : basis index
) {
    uint BasisID = g_RWActiveBasisIndexBuffer[InstanceID];
    SGData SG;
    WData W;
    FetchBasisData_W(BasisID, SG, W);
    float3 DiskOrigin;
    FetchBasisLocation(BasisID, DiskOrigin);

    float3 EyeDirection = normalize(DiskOrigin - g_CameraPosition);
    if(dot(EyeDirection, g_CameraDirection) < 0.2f) {
        // Degenerate the disk (out of field)
        InjectReprojectedBasis_Output Output;
        Output.Position = float4(0.f.xxx, 1.f);
        Output.PARAMS.x = asfloat(kGI10_InvalidId);
        return Output;
    }

    // Construct the basis
    // TODO : This does not take TAA into account
    float3 Tangent, Bitangent;
    TangentVectors(EyeDirection, Tangent, Bitangent);
    float Radian = TWO_PI * float(VertexIndex) / float(g_CR_DiskVertexCount);
    float SinTheta = sin(Radian);
    float CosTheta = cos(Radian);
    float3 WorldVertexDirection = SinTheta * Tangent + CosTheta * Bitangent;
    float3 WorldVertexPosition = 
        DiskOrigin + (
            g_CR_DiskRadiusMultiplier * g_RWBasisEffectiveRadiusBuffer[BasisID]
         + g_CR_DiskRadiusBias) * WorldVertexDirection;
    // Output
    InjectReprojectedBasis_Output Output;
    float3 HomoOrigin = transformPointProjection(DiskOrigin, g_CameraProjView);
    float3 HomoVertex = transformPointProjection(WorldVertexPosition, g_CameraProjView);
    // Ensure that the rasterization is conservative by making the disk slightly larger
    float2 ExpandVector    = HomoVertex.xy - HomoOrigin.xy;
    float2 ExpandDirection = normalize(ExpandVector);
    // The output is within clip space
    Output.Position    = float4(HomoVertex + float3(ExpandDirection * g_TileDimensionsInv * 1.42f, 0), 1.f);
    Output.PARAMS.x    = asfloat(BasisID);
    Output.PARAMS.yz   = HomoOrigin.xy;
    Output.PARAMS.w    = dot(DiskOrigin - g_CameraPosition, g_CameraDirection);
    return Output;
}

struct DebugBasis_Output {
    float4 Position                   : SV_Position;
    nointerpolation float4 BasisIndex : TEXCOORD0;
};


DebugBasis_Output DebugSSRC_Basis (
    in uint VertexIndex : SV_VertexID, // Vertex index
    in uint InstanceID  : SV_InstanceID // Instance Index : basis index
) {
    uint BasisID = g_RWActiveBasisIndexBuffer[InstanceID];
    SGData SG;
    WData W;
    FetchBasisData_W(BasisID, SG, W);
    float3 DiskOrigin;
    FetchBasisLocation(BasisID, DiskOrigin);

    float3 EyeDirection = normalize(DiskOrigin - g_CameraPosition);
    if(dot(EyeDirection, g_CameraDirection) < 0.2f) {
        // Degenerate the disk (out of field)
        DebugBasis_Output Output;
        Output.Position = float4(0.f.xxx, 1.f);
        Output.BasisIndex.x = asfloat(kGI10_InvalidId);
        return Output;
    }

    // Construct the basis
    // TODO : This does not take TAA into account
    float3 Tangent, Bitangent;
    TangentVectors(EyeDirection, Tangent, Bitangent);
    float Radian = TWO_PI * float(VertexIndex) / float(g_CR_DiskVertexCount);
    float SinTheta = sin(Radian);
    float CosTheta = cos(Radian);
    float3 WorldVertexDirection = SinTheta * Tangent + CosTheta * Bitangent;
    float3 WorldVertexPosition = DiskOrigin 
        + (g_CR_DiskRadiusMultiplier * g_RWBasisEffectiveRadiusBuffer[BasisID] 
            + g_CR_DiskRadiusBias) * WorldVertexDirection;
    // Output
    DebugBasis_Output Output;
    float3 HomoOrigin = transformPointProjection(DiskOrigin, g_CameraProjView);
    float3 HomoVertex = transformPointProjection(WorldVertexPosition, g_CameraProjView);
    // Ensure that the rasterization is conservative by making the disk slightly larger
    float2 ExpandVector       = HomoVertex.xy - HomoOrigin.xy;
    float  ExpandVectorLength = length(ExpandVector);
    float3 ExpandDirection    = (HomoVertex - HomoOrigin) / ExpandVectorLength;
    // The output is within clip space
    if(g_DebugVisualizeMode >= 3) {
        Output.Position = float4(HomoOrigin + (ExpandDirection * float3(g_OutputDimensionsInv, 1.f) * 1.5f), 1.f);
    } else if(g_DebugVisualizeMode == 2) {
        Output.Position = float4(HomoVertex.xy, float(BasisID) / g_MaxBasisCount, 1.f);
    } else if(g_DebugVisualizeMode == 1) {
        Output.Position = float4(HomoOrigin + (ExpandDirection * float3(g_OutputDimensionsInv, 1.f) * 1.5f), 1.f);
    } else {
        Output.Position = float4(HomoVertex + (ExpandDirection * float3(g_TileDimensionsInv, 1.f) * 1.42f), 1.f);
    }
    Output.BasisIndex.x = asfloat(BasisID);
    return Output;
}


struct DebugBasis3D_Output {
    float4 Position                   : SV_Position;
    float4 Color                      : COLOR;
    nointerpolation float4 BasisIndex : TEXCOORD0;
};

DebugBasis3D_Output DebugSSRC_Basis3D (
    in uint VertexIndex : SV_VertexID, // Vertex index
    in uint InstanceID  : SV_InstanceID // Instance Index : basis index
) {
    uint BasisID = g_RWActiveBasisIndexBuffer[InstanceID];
    SGData SG;
    WData W;
    FetchBasisData_W(BasisID, SG, W);
    float3 BasisOrigin;
    FetchBasisLocation(BasisID, BasisOrigin);

    float3 EyeDirection = normalize(BasisOrigin - g_CameraPosition);
    if(dot(EyeDirection, g_CameraDirection) < 0.2f) {
        // Degenerate the disk (out of field)
        DebugBasis3D_Output Output;
        Output.Position = float4(0.f.xxx, 1.f);
        Output.Color    = 0.f.xxxx;
        Output.BasisIndex.x = asfloat(kGI10_InvalidId);
        return Output;
    }

    // Output
    DebugBasis3D_Output Output;
    Output.BasisIndex.x = asfloat(BasisID);
    if(VertexIndex == 0) {
        Output.Position = float4(transformPointProjection(BasisOrigin, g_CameraProjView), 1.f);
        Output.Color.x  = 0.f;
    } else {
        float  VecLength = 0.f;
        if(g_DebugVisualizeMode == 0) {
            VecLength = dot(SG.Color, 1.f.xxx) * 0.02f;
        } else if(g_DebugVisualizeMode == 1) {
            VecLength = SG.Lambda * 0.006f;
        } else if(g_DebugVisualizeMode == 2) {
            VecLength = W.Alpha * 0.02f;
        } else if(g_DebugVisualizeMode == 3) {
            VecLength = g_RWBasisEffectiveRadiusFilmBuffer[BasisID] * 0.01f;
        }
        VecLength += 0.004f;
        float3 BasisVector  = SG.Direction * VecLength;
        Output.Position     = float4(transformPointProjection(BasisOrigin + BasisVector, g_CameraProjView), 1.f);
        Output.Color.x      = VecLength;
    }
    return Output;
}

struct DebugIncidentRadiance_Output {
    float4 Position : SV_Position;
    float4 Color    : COLOR;
};

DebugIncidentRadiance_Output DebugSSRC_IncidentRadiance (
    in uint VertexIndex : SV_VertexID // Vertex index
) {
    float3 Origin    = g_RWDebugCursorWorldPosBuffer[0];
    int    Index     = VertexIndex;
    float3 Direction = FibonacciSphere(Index, g_DebugVisualizeIncidentRadianceNumPoints);
    float3 Radiance  = g_RWDebugVisualizeIncidentRadianceBuffer[Index];
    float3 World     = Origin + (Direction * dot(Radiance, 1.f.xxx)) * 0.1f;
    
    DebugIncidentRadiance_Output Output;
    Output.Position  = float4(transformPointProjection(World, g_CameraProjView), 1.f);
    Output.Color     = float4(Radiance, 1.f);
    return Output;
}

struct DebugUpdateRays_Output {
    float4 Position : SV_Position;
    float4 Color    : COLOR;
};

DebugUpdateRays_Output DebugSSRC_UpdateRays (
    in uint VertexIndex : SV_VertexID, // Vertex index
    in uint InstanceID  : SV_InstanceID // Instance Index : ray rank
) {
    int2   TexCoords      = g_DebugCursorPixelCoords;
    int2   TileCoords     = int2(TexCoords.x / SSRC_TILE_SIZE, TexCoords.y / SSRC_TILE_SIZE);
    int    TileID         = TileCoords.x + TileCoords.y * g_TileDimensions.x;
    int RayRank   = InstanceID;
    int RayOffset = g_RWTileRayOffsetBuffer[TileID];
    int RayCount  = g_RWTileRayCountBuffer[TileID];
    int2 RayOriginCoords    = UnpackUint16x2(g_RWUpdateRayOriginBuffer[RayOffset + RayRank]);
    float Depth             = g_DepthTexture.Load(int3(RayOriginCoords, 0)).x;
    float2 UV               = (float2(RayOriginCoords) + 0.5f) * g_OutputDimensionsInv;
    float3 RayOrigin        = InverseProject(g_CameraProjViewInv, UV, Depth);
    float3 RayDirection     = UnpackNormal(g_RWUpdateRayDirectionBuffer[RayOffset + RayRank]);
    float3 RayRadiance      = UnpackFp16x3(g_RWUpdateRayRadiancePdfBuffer[RayOffset + RayRank]);
    float3 World;
    if(VertexIndex == 0) {
        World = RayOrigin;
    } else {
        World = RayOrigin + RayDirection * (dot(RayRadiance, 1.f.xxx) * 0.1f + 0.01f);
    }
    DebugUpdateRays_Output Output;
    Output.Position  = float4(transformPointProjection(World, g_CameraProjView), 1.f);
    Output.Color     = float4(0.5f * (RayDirection + 1.f), 1.f);
    return Output;
}