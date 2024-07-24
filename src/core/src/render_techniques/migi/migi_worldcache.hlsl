#ifndef MIGI_WORLDCACHE_HLSL
#define MIGI_WORLDCACHE_HLSL

#include "migi_inc.hlsl"
#include "migi_lib.hlsl"

struct WorldCacheProbeHeader {
    int4 GridCoords;
    // [-1, 1] Normalized
    float3 GridInternalNormalizedPosition;
    float3 WorldPosition;
    int   Score;
    bool  bActive;
};

struct WorldCacheVisibility {
    uint InstanceIndex;
    uint GeometryIndex;
    uint PrimitiveIndex;
    float2 Barycentrics;
    bool IsFrontFace;
};

// The number of cache queries requested within a frame.
RWStructuredBuffer<uint> g_RWWorldCacheQueryCountBuffer;
// Queries, packed in visibility
RWStructuredBuffer<uint4> g_RWWorldCacheRawQueryVisibilityBuffer;
RWStructuredBuffer<uint4> g_RWWorldCacheQueryVisibilityBuffer;
// Queries, direction, octahedron encoded
RWStructuredBuffer<uint> g_RWWorldCacheRawQueryDirectionBuffer;
RWStructuredBuffer<uint> g_RWWorldCacheQueryDirectionBuffer;

// World cache probe atlas
RWTexture2D<float4> g_RWWorldCacheIrradianceLuminanceTexture;
RWTexture2D<float2> g_RWWorldCacheDepthDepthSquaredTexture;
// Sum of probe irradiances in a single pixel per probe
RWTexture2D<float4> g_RWWorldCacheProbeIrradianceTexture;
// 3D clipmaps, -1 means empty, -2 means spawn requested 
RWTexture3D<int> g_RWWorldCacheProbeIndexTexture[MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES];
RWStructuredBuffer<uint> g_RWWorldCacheProbeSpawnRequestBuffer;
RWStructuredBuffer<uint> g_RWWorldCacheProbeSpawnRequestCountBuffer;
// Probe headers, packed in float4
RWStructuredBuffer<float4> g_RWWorldCacheProbeHeaderBuffer;
// Free probe indices
RWStructuredBuffer<uint> g_RWWorldCacheFreeProbeIndexBuffer;
// Number of active probes
RWStructuredBuffer<uint> g_RWWorldCacheActiveProbeCountBuffer;
// List of active probes, generated every frame and used for ray dispatching
RWStructuredBuffer<uint> g_RWWorldCacheActiveProbeIndexBuffer;

uint4 WorldCache_PackQueryVisibility (WorldCacheVisibility Visibility) {
    // 1 + 15 + 16 + 32 + 32 x 2
    return uint4(
        (Visibility.GeometryIndex | (Visibility.InstanceIndex<<16) | 
        (Visibility.IsFrontFace ? 0 : 0x80000000u)),
        (Visibility.PrimitiveIndex),
        asuint(Visibility.Barycentrics.x),
        asuint(Visibility.Barycentrics.y)
    );
}
WorldCacheVisibility WorldCache_UnpackQueryVisibility (uint4 Packed) {
    WorldCacheVisibility Visibility;
    Visibility.GeometryIndex = Packed.x & 0xffffu;
    Visibility.InstanceIndex = (Packed.x >> 16) & 0x7fffu;
    Visibility.IsFrontFace = (Packed.x & 0x80000000u) == 0;
    Visibility.PrimitiveIndex = Packed.y;
    Visibility.Barycentrics = float2(
        asfloat(Packed.z), asfloat(Packed.w)
    );
    return Visibility;
}

WorldCacheVisibility WorldCache_FetchQueryVisibility (uint QueryIndex) {
    uint4 Packed = g_RWWorldCacheQueryVisibilityBuffer[QueryIndex];
    return WorldCache_UnpackQueryVisibility(Packed);
}

void WorldCache_WriteQueryVisibility (uint QueryIndex, WorldCacheVisibility Visibility) {
    g_RWWorldCacheQueryVisibilityBuffer[QueryIndex] = WorldCache_PackQueryVisibility(Visibility);
}
void WorldCache_WriteQueryDirection (uint QueryIndex, float3 Direction) {
    float2 Oct = UnitVectorToOctahedron(Direction);
    g_RWWorldCacheQueryDirectionBuffer[QueryIndex] = PackUnorm2x16Unbiased(Oct);
}


uint WorldCache_GetActiveProbeCount () {
    return g_RWWorldCacheActiveProbeCountBuffer[0];
}

int3 WorldCache_GetAbsoluteGridCoords (int4 GridCoords) {
    int4 Offsets = 0;
    switch(GridCoords.w) {
        case 0: Offsets = WorldCache.VolumeCascadeGridCoordOffsets[0]; break;
        case 1: Offsets = WorldCache.VolumeCascadeGridCoordOffsets[1]; break;
        case 2: Offsets = WorldCache.VolumeCascadeGridCoordOffsets[2]; break;
        case 3: Offsets = WorldCache.VolumeCascadeGridCoordOffsets[3]; break;
    }
    return (GridCoords.xyz + Offsets.xyz) * (1 << GridCoords.w);
}

float3 WorldCache_GetProbeWorldCenterFromGridCoords (int4 GridCoords) {
    float GridSize = WorldCache.GridSize * (1 << GridCoords.w);
    return WorldCache.VolumeMin +
        WorldCache_GetAbsoluteGridCoords(GridCoords) * GridSize;
}

bool WorldCache_IsProbeCoordsOutOfBounds (int4 GridCoords) {
    if(any(GridCoords.xyz >= WorldCache.GridCoordsBound)) {
        return true;
    }
    return false;
}

bool WorldCache_IsProbeCoordsOverlapped (int4 GridCoords) {
    // Check if it's overlapped with finer level
    if(GridCoords.w > 0) {
        int3 LowerLevelCoords = 
            GridCoords.xyz * 2 
            - WorldCache.VolumeCascadeGridCoordOffsets[GridCoords.w - 1].xyz;
        if(all(LowerLevelCoords < WorldCache.GridCoordsBound)
        && all(LowerLevelCoords >= 0)) {
            return true;
        }
    }
    return false;
}

uint WorldCache_GetProbeScore (int ProbeIndex) {
    return asuint(g_RWWorldCacheProbeHeaderBuffer[ProbeIndex].x) >> 27;
}

void  WorldCache_WriteProbeScore (int ProbeIndex, uint Score) {
    g_RWWorldCacheProbeHeaderBuffer[ProbeIndex].x = 
        (asuint(g_RWWorldCacheProbeHeaderBuffer[ProbeIndex].x) & 0x7ffffffu) | (Score << 27);
}

WorldCacheProbeHeader WorldCache_UnpackProbeHeader (uint4 Packed) {
    WorldCacheProbeHeader Header;
    // Decode grid coords & scores from a single uint (x:8, y:8, z:8, level:3, score:5)
    Header.GridCoords = int4(
        Packed.x & 0xff,
        (Packed.x >> 8) & 0xff,
        (Packed.x >> 16) & 0xff,
        (Packed.x >> 24) & 0x7
    );
    Header.Score = Packed.x >> 27;
    // Score 0 means inactive
    Header.bActive = Header.Score != 0;
    // Decode grid internal position
    Header.GridInternalNormalizedPosition = float3(
        asfloat(Packed.y),
        asfloat(Packed.z),
        asfloat(Packed.w)
    );
    // Compute world position
    Header.WorldPosition = WorldCache_GetProbeWorldCenterFromGridCoords(Header.GridCoords) 
        + Header.GridInternalNormalizedPosition * WorldCache.HalfGridSize;
    return Header;
}

uint4 WorldCache_PackProbeHeader (WorldCacheProbeHeader Header) {
    uint4 Packed;
    // Decode grid coords & scores from a single uint (x:8, y:8, z:8, level:3, score:5)
    Packed.x = Header.GridCoords.x & 0xff;
    Packed.x |= (Header.GridCoords.y & 0xff) << 8;
    Packed.x |= (Header.GridCoords.z & 0xff) << 16;
    Packed.x |= (Header.GridCoords.w & 0x7) << 24;
    Packed.x |= Header.Score << 27;
    // Encode grid internal position
    Packed.y = asuint(Header.GridInternalNormalizedPosition.x);
    Packed.z = asuint(Header.GridInternalNormalizedPosition.y);
    Packed.w = asuint(Header.GridInternalNormalizedPosition.z);
    return Packed;
}

WorldCacheProbeHeader WorldCache_GetProbeHeader (int Index) {
    return WorldCache_UnpackProbeHeader(g_RWWorldCacheProbeHeaderBuffer[Index]);
}

void WorldCache_WriteProbeHeader(int Index, WorldCacheProbeHeader Header) {
    g_RWWorldCacheProbeHeaderBuffer[Index] = WorldCache_PackProbeHeader(Header);
}

// Get the min probe coords of the grid that the position is in
int4 WorldCache_GetProbeGridCoordsBase (float3 Position) {
    float3 LocalPosition = Position - WorldCache.VolumeMin;
    float3 AbsoluteGridPosition  = LocalPosition * WorldCache.GridSizeInv;
    int ClipmapLevel = WorldCache.MaxClipmapCascades;
    // fine-to-coarse grain search
    float3 LevelGridPosition;
    [unroll(WORLD_CACHE_MAX_CLIPMAP_CASCADES)]
    for(int i = 0; i < WorldCache.MaxClipmapCascades; i--) {
        LevelGridPosition = AbsoluteGridPosition * (1.f / (1 << i)) - WorldCache.VolumeCascadeGridCoordOffsets[i].xyz;
        if(all(LevelGridPosition >= 0)
        && all(LevelGridPosition <= (WorldCache.GridCoordsBound - 1))) {
            ClipmapLevel = i;
            break;
        }
    }
    return int4(
        floor(LevelGridPosition.x),
        floor(LevelGridPosition.y),
        floor(LevelGridPosition.z),
        ClipmapLevel
    );
}

void WorldCache_RecycleProbe (int ProbeIndex) {
    int FreeProbeListIndex = WorldCache.NumProbes - InterlockedAdd(g_RWWorldCacheActiveProbeCountBuffer[0], -1);
    g_RWWorldCacheFreeProbeIndexBuffer[FreeProbeListIndex] = ProbeIndex;
}

int WorldCache_AllocateProbe () {
    int FreeProbeListIndex = WorldCache.NumProbes - InterlockedAdd(g_RWWorldCacheActiveProbeCountBuffer[0], 1) - 1;
    if(FreeProbeListIndex < 0) {
        return -1;
    }
    return g_RWWorldCacheFreeProbeIndexBuffer[FreeProbeListIndex];
}

struct WorldCacheProbeSpawnRequest {
    int4 GridCoords;
};

WorldCacheProbeSpawnRequest WorldCache_UnpackProbeSpawnRequest (uint Packed) {
    WorldCacheProbeSpawnRequest Request;
    Request.GridCoords = int4(
        Packed & 0xff,
        (Packed >> 8) & 0xff,
        (Packed >> 16) & 0xff,
        (Packed >> 24) & 0x7
    );
    return Request;
}

uint WorldCache_PackProbeSpawnRequest (WorldCacheProbeSpawnRequest Request) {
    return Request.GridCoords.x & 0xff |
        (Request.GridCoords.y & 0xff) << 8 |
        (Request.GridCoords.z & 0xff) << 16 |
        (Request.GridCoords.w & 0x7) << 24;
}

WorldCacheProbeSpawnRequest WorldCache_GetProbeSpawnRequest (int RequestIndex) {
    return WorldCache_UnpackProbeSpawnRequest(g_RWWorldCacheProbeSpawnRequestBuffer[RequestIndex]);
}

void WorldCache_AddProbeSpawnRequest (WorldCacheProbeSpawnRequest Request) {
    int RequestIndex = InterlockedAdd(g_RWWorldCacheProbeSpawnRequestCountBuffer[0], 1);
    g_RWWorldCacheProbeSpawnRequestBuffer[RequestIndex] = WorldCache_PackProbeSpawnRequest(Request);
}

int WorldCache_GetProbeIndexFromGrid (int4 GridCoords) {
    switch(GridCoords.w) {
        case 0: return g_RWWorldCacheProbeIndexTexture[0][GridCoords.xyz];
        case 1: return g_RWWorldCacheProbeIndexTexture[1][GridCoords.xyz];
        case 2: return g_RWWorldCacheProbeIndexTexture[2][GridCoords.xyz];
        case 3: return g_RWWorldCacheProbeIndexTexture[3][GridCoords.xyz];
    }
    return -1;
}

void WorldCache_WriteProbeIndexToGrid (int4 GridCoords, int ProbeIndex) {
    switch(GridCoords.w) {
        case 0: g_RWWorldCacheProbeIndexTexture[0][GridCoords.xyz] = ProbeIndex; break;
        case 1: g_RWWorldCacheProbeIndexTexture[1][GridCoords.xyz] = ProbeIndex; break;
        case 2: g_RWWorldCacheProbeIndexTexture[2][GridCoords.xyz] = ProbeIndex; break;
        case 3: g_RWWorldCacheProbeIndexTexture[3][GridCoords.xyz] = ProbeIndex; break;
    }
}

int2 WorldCache_GetProbeAtlasBase (int ProbeIndex) {
    int2 ProbeCoords = int2(
        ProbeIndex % WorldCache.ProbeAtlasWidth,
        ProbeIndex / WorldCache.ProbeAtlasWidth
    );
    return ProbeCoords * WORLD_CACHE_PROBE_RESOLUTION;
}

struct WorldCacheSample {
    int ProbeIndex[4];
    float4 Weights;
};

WorldCacheSample WorldCache_SampleProbes (float3 WorldPosition, bool bPrevious = false) {
    WorldCacheSample Sample;
    int4 GridCoords = WorldCache_GetProbeGridCoordsBase(WorldPosition, bPrevious);

}

void WorldCache_TouchSample (WorldCacheSample Sample) {
    for(int i = 0; i<4; i++) {
        if(Sample.ProbeIndex[i] != -1) {
            int Score = WorldCache_GetProbeScore(Sample.ProbeIndex[i]);
            Score = min(Score + WorldCache.ProbeScoreBonus, WorldCache.ProbeInitialScore);
            WorldCache_WriteProbeScore(Sample.ProbeIndex[i], Score);
        }
    }
}



#endif // MIGI_WORLDCACHE_HLSL