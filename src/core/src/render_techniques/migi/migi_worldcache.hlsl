#ifndef MIGI_WORLDCACHE_HLSL
#define MIGI_WORLDCACHE_HLSL
struct WorldCacheQuery {
    float3 Hit;
    float3 Direction;
    float HitDistance;
};

struct WorldCacheProbeHeader {
    float3 Position;
    float  Score;
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
RWStructuredBuffer<uint> g_RWWorldCacheProbeCountBuffer;
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


#endif // MIGI_WORLDCACHE_HLSL