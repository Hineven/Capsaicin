#ifndef MIGI_PROBES_HLSL
#define MIGI_PROBES_HLSL

#include "migi_lib.hlsl"

struct ProbeHeader {
    // Screen pixel position of the probe
    int2 ScreenPosition;
    int BasisOffset;
    // 0: 0, 1: 1, 2: 2, 3: 4, 4: 8, no larger than 8
    int  Class;
    bool bValid;
    float  LinearDepth;
    float3 Position;
    float3 Normal;
};  

struct SSRC_SampleData {
    // Base atlas coords
    int2 Index[4];
    // Interpolation weights
    float4 Weights;
};

ProbeHeader GetScreenProbeHeader (int2 ProbeIndex, bool bPrevious = false) {
    uint Packed = bPrevious ? g_RWPreviousProbeHeaderPackedTexture[ProbeIndex] : g_RWProbeHeaderPackedTexture[ProbeIndex];
    ProbeHeader Header;
    Header.BasisOffset  = Packed & 0x00FFFFFF;
    Header.Class        = (Packed >> 24) & 0x0F;
    uint   Flags        = (Packed >> 28) & 0x0F;
    Header.ScreenPosition = UnpackUint16x2(
        bPrevious
        ? g_RWPreviousProbeScreenPositionTexture.Load(int3(ProbeIndex, 0)).x
        : g_RWProbeScreenPositionTexture.Load(int3(ProbeIndex, 0)).x
    );
    Header.LinearDepth  = 
        bPrevious ? g_RWPreviousProbeLinearDepthTexture.Load(int3(ProbeIndex, 0)).x
        : g_RWProbeLinearDepthTexture.Load(int3(ProbeIndex, 0)).x;
    Header.Position     = 
        bPrevious ? g_RWPreviousProbePositionTexture.Load(int3(ProbeIndex, 0)).xyz
        : g_RWProbeWorldPositionTexture.Load(int3(ProbeIndex, 0)).xyz;
    Header.Normal       = 
        OctahedronToUnitVector(
            (bPrevious ? g_RWPreviousProbeNormalTexture.Load(int3(ProbeIndex, 0)).xy 
            : g_RWProbeNormalTexture.Load(int3(ProbeIndex, 0)).xy)
             * 2.f - 1.f);
            
    Header.bValid       =  Header.LinearDepth > 0;
    return Header;
}

void WriteScreenProbeHeader (int2 ProbeIndex, ProbeHeader Header) {
    uint Packed;
    Packed = Header.BasisOffset & 0x00FFFFFF;
    Packed |= (Header.Class & 0x0F) << 24;
    // Packed |= (Header.bValid ? 1 : 0) << 28;
    g_RWProbeHeaderPackedTexture[ProbeIndex] = Packed;
    g_RWProbeScreenPositionTexture[ProbeIndex] = PackUint16x2(Header.ScreenPosition);
    g_RWProbeLinearDepthTexture[ProbeIndex] = Header.LinearDepth;
    g_RWProbeWorldPositionTexture[ProbeIndex] = Header.Position;
    g_RWProbeNormalTexture[ProbeIndex] = UnitVectorToOctahedron(Header.Normal * 0.5f + 0.5f);
}

int2 GetTileJitter (bool bPrevious = false) {
    return Hammersley16((bPrevious ? MI.PreviousFrameSeed : MI.FrameSeed) % 8, 8, 0) * SSRC_TILE_SIZE;
}

int2 GetScreenProbeScreenPosition (int2 ProbeIndex, bool bPrevious = false) {
    int2 TileJitter = GetTileJitter(bPrevious);
    int2 UniformScreenProbeScreenPosition = ProbeIndex * SSRC_TILE_SIZE + TileJitter;
    if(any(ProbeIndex >= MI.TileDimensions)) {
        ProbeHeader Header = GetScreenProbeHeader(ProbeIndex, bPrevious);
        UniformScreenProbeScreenPosition = Header.ScreenPosition;
    }
    return UniformScreenProbeScreenPosition;
}

float3 GetScreenProbePosition (int2 ProbeIndex, bool bPrevious = false) {
    return bPrevious ? g_RWPreviousProbePositionTexture.Load(int3(ProbeIndex, 0)).xyz
        : g_RWProbeWorldPositionTexture.Load(int3(ProbeIndex, 0)).xyz;
}

int ComputeProbeRankFromSplattedError (int2 ScreenCoords) {
    // TODO: Implement this function
    return 0;
}

int GetProbeBasisCountFromClass (int ProbeClass) {
    return (ProbeClass > 0) ? (1u << (ProbeClass - 1)) : 0;
}

// Get the coords of a probe within the adaptive probe index texture
int2 GetAdaptiveProbeIndexCoords (int2 TileCoords, int AdaptiveProbeListIndex) {
	int2 CoordsWithinTile = int2(
        AdaptiveProbeListIndex % SSRC_TILE_SIZE,
        AdaptiveProbeListIndex / SSRC_TILE_SIZE
    );
	return CoordsWithinTile * MI.TileDimensions + TileCoords;
}

int  GetAdaptiveProbeIndex (int2 TileCoords, int AdaptiveProbeListIndex, bool bPrevious = false) {
    int2 IndexCoords = GetAdaptiveProbeIndexCoords(TileCoords, AdaptiveProbeListIndex);
    return bPrevious ? g_RWPreviousTileAdaptiveProbeIndexTexture.Load(int3(IndexCoords, 0)).x
        : g_RWTileAdaptiveProbeIndexTexture.Load(int3(IndexCoords, 0)).x;
}

int2 GetUniformScreenProbeScreenPosition (int2 TileCoords, bool bPrevious = false) {
    return TileCoords * SSRC_TILE_SIZE + GetTileJitter(bPrevious);
}

float2 GetUniformScreenProbeScreenUV (int2 TileCoords, bool bPrevious = false) {
    return (GetUniformScreenProbeScreenPosition(TileCoords, bPrevious) + 0.5) * MI.ScreenDimensionsInv;
}

float GetScreenProbeLinearDepth (int2 ProbeIndex, bool bPrevious = false) {
    ProbeHeader Header = GetScreenProbeHeader(ProbeIndex, bPrevious);
    return Header.LinearDepth;
}

int   GetScreenProbeBasisOffset (int2 ProbeIndex, bool bPrevious = false) {
    ProbeHeader Header = GetScreenProbeHeader(ProbeIndex, bPrevious);
    return Header.BasisOffset;
}

float3 GetScreenProbeNormal (int2 ProbeIndex, bool bPrevious = false) {
    return bPrevious
        ? OctahedronToUnitVector(g_RWPreviousProbeNormalTexture.Load(int3(ProbeIndex, 0)).xy * 2.f - 1.f)
        : OctahedronToUnitVector(g_RWProbeNormalTexture.Load(int3(ProbeIndex, 0)).xy * 2.f - 1.f);
}

struct ScreenProbeMaterial {
    float3 Position;
    float  Depth;
    float3 GeometryNormal;
    bool   bValid;
};

ScreenProbeMaterial FetchScreenProbeMaterial (int2 ScreenCoords, bool HiRes) {
    ScreenProbeMaterial Material;
    Material.Depth = g_RWProbeLinearDepthTexture.Load(int3(ScreenCoords, 0)).x;
    Material.GeometryNormal = OctahedronToUnitVector(
        g_RWProbeNormalTexture.Load(int3(ScreenCoords, 0)).xy * 2.f - 1.f);
    Material.bValid = Material.Depth > 0;
    float2 UV = (ScreenCoords + 0.5f) * MI.ScreenDimensionsInv;
    Material.Position = 
        HiRes 
        ? RecoverWorldPositionHiRes(ScreenCoords)
        : InverseProject(MI.CameraProjViewInv, UV, Material.Depth);
    return Material;
}

float3 GetScreenProbeIrradiance (int2 Index) {
    return g_RWProbeIrradianceTexture[Index].xyz;
}

void   WriteScreenProbeIrradiance (int2 Index, float3 Irradiance) {
    g_RWProbeIrradianceTexture[Index] = float4(Irradiance, 0);
}


#endif // MIGI_PROBES_HLSL