#ifndef MIGI_WORLDCACHE_HLSL
#define MIGI_WORLDCACHE_HLSL

#include "migi_inc.hlsl"

struct WorldCacheQuery {
    float3 Hit;
    float3 Direction;
    float HitDistance;
};

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
RWStructuredBuffer<uint4> g_RWWorldCacheQueryVisibilityBuffer;
// Queries, direction, octahedron encoded
RWStructuredBuffer<uint> g_RWWorldCacheQueryDirectionBuffer;

// World cache probe atlas
RWTexture2D<float4> g_RWWorldCacheIrradianceLuminanceTexture;
RWTexture2D<float2> g_RWWorldCacheDepthDepthSquaredTexture;
// Sum of probe irradiances in a single pixel per probe
RWTexture2D<float4> g_RWWorldCacheProbeIrradianceTexture;
// Folded 3d clipmaps, 0xffffffff means invalid
RWTexture2D<uint> g_RWWorldCacheProbeIndexTexture;
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
        (Visibility.primitive_index),
        asuint(Visibility.barycentrics.x),
        asuint(Visibility.barycentrics.y);
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


uint WorldCache_GetActiveProbeCount () {
    return g_RWWorldCacheActiveProbeCountBuffer[0];
}

float WorldCache_GetGridWorldCenter (int4 GridCoords) {
    float GridSize = WorldCache.GridSize * (1 << GridCoords.w);
    return WorldCache.Center + GridCoords.xyz * GridSize;
}

bool WorldCache_IsGridCoordsOutOfBounds (int4 GridCoords) {
    if(any(abs(GridCoords).xyz >= WorldCache.GridCoordsBound)) {
        return true;
    }
    // Check if it's overlapped with higher level
    if(GridCoords.w > 0) {
        // Check if the farthest border is within the bound
        if(all(abs(GridCoords.xyz) * 2 + 1 < WorldCache.GridCoordsBound)) {
            return true;
        }
    }
    return false;
}

WorldCacheProbeHeader WorldCache_UnpackProbeHeader (uint4 Packed) {
    WorldCacheProbeHeader Header;
    // Decode grid coords & scores from a single uint (x:7, y:7, z:7, level:3, score:8)
    Header.GridCoords = int4(
        Packed.x & 0x7f,
        (Packed.x >> 7) & 0x7f,
        (Packed.x >> 14) & 0x7f,
        (Packed.x >> 21) & 0x7
    );
    Header.GridCoords.xyz -= 0x40;
    Header.Score = Packed.x >> 24;
    // Score 0 means inactive
    Header.bActive = Header.Score != 0;
    // Decode grid internal position
    Header.GridInternalNormalizedPosition = float3(
        asfloat(Packed.y),
        asfloat(Packed.z),
        asfloat(Packed.w)
    );
    // Compute world position
    Header.WorldPosition = WorldCache_GetGridWorldCenter(Header.GridCoords) 
        + Header.GridInternalNormalizedPosition * WorldCache.HalfGridSize;
    return Header;
}

WorldCacheProbeHeader WorldCache_GetProbeHeader (int Index) {
    return WorldCache_UnpackProbeHeader(g_RWWorldCacheProbeHeaderBuffer[Index]);
}

int4 WorldCache_GetProbeGridCoords (float3 Position) {
    float3 LocalPosition = Position - WorldCache.Center;
    float3 GridPosition  = LocalPosition / WorldCache.GridSize;
    int ClipmapLevel = 0;
    if(any(abs(round(GridPosition))) >= WorldCache.GridCoordsBound) {
        ClipmapLevel ++;
        GridPosition /= 2;
    }
    if(any(abs(round(GridPosition))) >= WorldCache.GridCoordsBound) {
        ClipmapLevel ++;
        GridPosition /= 2;
    }
    if(any(abs(round(GridPosition))) >= WorldCache.GridCoordsBound) {
        ClipmapLevel ++;
        GridPosition /= 2;
    }
    return int4(
        round(GridPosition.x),
        round(GridPosition.y),
        round(GridPosition.z),
        ClipmapLevel
    );
}

#endif // MIGI_WORLDCACHE_HLSL