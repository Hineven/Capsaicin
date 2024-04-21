// Shared structs among CPU and GPU
#include "../../gpu_shared.h"

// Parameters & Globals & Helper functions
#include "migi_inc.hlsl"

// Library for MIGI
#include "migi_lib.hlsl"


struct InjectReprojectedBasis_Input {
    float4 Position                   : SV_Position;
    nointerpolation float4 PARAMS     : TEXCOORD0;
    float4 PARAMS1                    : TEXCOORD1;
};

void SSRC_InjectReprojectedBasis (
    in InjectReprojectedBasis_Input Input
) {
    uint BasisIndex = asuint(Input.PARAMS.x);
    if(BasisIndex == kGI10_InvalidId) {
        // Degenerate the disk
        return;
    }
    int2 TileCoords = uint2(Input.Position.xy);

    float HiZ_Min = g_TileHiZ_Min.Load(int3(TileCoords, 0)).x;
    float HiZ_Max = g_TileHiZ_Max.Load(int3(TileCoords, 0)).x;
    // Fully empty tile, no injection
    if(HiZ_Min == 1.f) return ;
    
    // Intersect the sphere with the tile's truncated pyramid (rough approximation)
    float PyramidMin = GetLinearDepth(HiZ_Min);
    float PyramidMax = GetLinearDepth(HiZ_Max);
    float2 MinTileFilm        = TileCoords * SSRC_TILE_SIZE;
    float2 MaxTileFilm        = MinTileFilm + SSRC_TILE_SIZE.xx;
    float EffectiveRadius     = g_RWBasisEffectiveRadiusBuffer[BasisIndex];
    float EffectiveRadiusFilm = g_RWBasisEffectiveRadiusFilmBuffer[BasisIndex];

    float2 BasisNDC2          = Input.PARAMS.yz;
    float2 BasisFilm          = NDC22UV(BasisNDC2) * g_OutputDimensions.xy;
    float BasisLinearDistance = Input.PARAMS.w;

    bool Inject = true;
    // Clip condition 1
    if(BasisLinearDistance < PyramidMin || BasisLinearDistance > PyramidMax) {
        float MinDistance = min(abs(BasisLinearDistance - PyramidMin), abs(BasisLinearDistance - PyramidMax));
        if(MinDistance > EffectiveRadius) {
            Inject = false;
        }
    }
    // Clip condition 2
    float MinNDCDistX = 0.f;
    float MinNDCDistY = 0.f;
    if(BasisFilm.x < MinTileFilm.x || BasisFilm.x > MaxTileFilm.x) {
        MinNDCDistX = min(abs(BasisFilm.x - MinTileFilm.x), abs(BasisFilm.x - MaxTileFilm.x));
    }
    if(BasisFilm.y < MinTileFilm.y || BasisFilm.y > MaxTileFilm.y) {
        MinNDCDistY = min(abs(BasisFilm.y - MinTileFilm.y), abs(BasisFilm.y - MaxTileFilm.y));
    }
    if(MinNDCDistX*MinNDCDistX + MinNDCDistY*MinNDCDistY > EffectiveRadiusFilm*EffectiveRadiusFilm) {
        Inject = false;
    }

    // Insert the basis index into the tile
    if(Inject) ScreenCache_InjectBasisIndexToTile(TileCoords, BasisIndex);
}


struct DebugBasis_Input {
    float4 Position                   : SV_Position;
    nointerpolation float4 BasisIndex : TEXCOORD0;
};

float4 DebugSSRC_Basis (
    in DebugBasis_Input Input
) : SV_Target {
    uint BasisIndex = asuint(Input.BasisIndex.x);
    if(BasisIndex == kGI10_InvalidId) {
        // Degenerate the disk
        discard;
    }
    if(g_DebugVisualizeMode == 3) {
        SGData SG;
        WData W;
        FetchBasisData_W(BasisIndex, SG, W);
        return float4((SG.Direction + 1.f) * 0.5f, 1.f);
    }
    return float4(BasisIndexToColor(BasisIndex), 1.f);
}

struct DebugBasis3D_Input {
    float4 Position                   : SV_Position;
    float4 Color                      : COLOR;
    nointerpolation float4 BasisIndex : TEXCOORD0;
};

float4 DebugSSRC_Basis3D (
    in DebugBasis3D_Input Input
) : SV_Target {
    uint BasisIndex = asuint(Input.BasisIndex.x);
    if(BasisIndex == kGI10_InvalidId) {
        // Degenerate the disk
        discard;
    }
    SGData SG;
    WData W;
    FetchBasisData_W(BasisIndex, SG, W);
    float Pos = Input.Color.x;
    if(Pos < 0.004f) return float4(0.f, 0.f, 0.f, 1.f);
    if(g_DebugVisualizeMode == 0) {
        return float4((SG.Direction + 1.f) * 0.5f, 1.f);
    } else {
        return float4((SG.Direction + 1.f) * 0.5f, 1.f);
    }
}