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


bool WorldCache_IsProbeIndexValid (uint ProbeIndex) {
    return 0 == (ProbeIndex & 0x80000000u);
}

// The number of cache queries requested within a frame.
RWStructuredBuffer< int> g_RWWorldCacheQueryCountBuffer;
// Queries, packed in visibility
RWStructuredBuffer<uint4> g_RWWorldCacheQueryVisibilityBuffer;
// Queries, direction, octahedron encoded
RWStructuredBuffer<uint> g_RWWorldCacheQueryDirectionBuffer;

// World cache probe atlas
// Actually, it stores Irradiance / (2PI)
RWTexture2D<float4> g_RWWorldCacheIrradiance2PLuminanceTexture;
Texture2D<float4> g_WorldCacheIrradiance2PLuminanceTexture;
RWTexture2D<float2> g_RWWorldCacheMomentumTexture;
Texture2D<float2> g_WorldCacheMomentumTexture;
RWTexture2D<float2> g_RWWorldCacheCOVTexture;
Texture2D<float2> g_WorldCacheCOVTexture;
// 3D clipmaps, 0xffffffff means empty, 0xfffffffe means spawn requested 
RWTexture3D<uint> g_RWWorldCacheProbeIndexTexture[MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES];
RWStructuredBuffer<uint> g_RWWorldCacheProbeSpawnRequestBuffer;
RWStructuredBuffer< int> g_RWWorldCacheProbeSpawnRequestCountBuffer;
// Probe headers, packed in float4
RWStructuredBuffer<uint4> g_RWWorldCacheProbeHeaderBuffer;
// Free probe indices
RWStructuredBuffer<uint> g_RWWorldCacheFreeProbeIndexBuffer;
// Number of active probes
RWStructuredBuffer< int> g_RWWorldCacheActiveProbeCountBuffer;
// List of active probes, generated every frame and used for ray dispatching
RWStructuredBuffer<uint> g_RWWorldCacheActiveProbeIndexBuffer;
// Mark probes being touched 
RWStructuredBuffer<uint> g_RWWorldCacheProbeTouchBuffer;

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
float3 WorldCache_FetchQueryDirection (uint QueryIndex) {
    float2 Oct01 = UnpackUnorm2x16Unbiased(g_RWWorldCacheQueryDirectionBuffer[QueryIndex]);
    return OctahedronToUnitVector01(Oct01);
}

void WorldCache_WriteQueryVisibility (uint QueryIndex, WorldCacheVisibility Visibility) {
    g_RWWorldCacheQueryVisibilityBuffer[QueryIndex] = WorldCache_PackQueryVisibility(Visibility);
}
void WorldCache_WriteQueryDirection (uint QueryIndex, float3 Direction) {
    float2 Oct01 = UnitVectorToOctahedron(Direction);
    g_RWWorldCacheQueryDirectionBuffer[QueryIndex] = PackUnorm2x16Unbiased(Oct01);
}

float WorldCache_GetGridSizeForLevel (int Level) {
    return WorldCache.GridSize * (1u << Level);
}

uint WorldCache_GetActiveProbeCount () {
    return g_RWWorldCacheActiveProbeCountBuffer[0];
}

int3 WorldCache_GetAbsoluteGridCoords (int4 GridCoords, bool bPrevious = false) {
    int4 Offsets = 0;
    if(bPrevious) {
        switch(GridCoords.w) {
            case 0: Offsets = WorldCache.PreviousVolumeCascadeGridCoordOffsets[0]; break;
            case 1: Offsets = WorldCache.PreviousVolumeCascadeGridCoordOffsets[1]; break;
            case 2: Offsets = WorldCache.PreviousVolumeCascadeGridCoordOffsets[2]; break;
            case 3: Offsets = WorldCache.PreviousVolumeCascadeGridCoordOffsets[3]; break;
        }
    } else {
        switch(GridCoords.w) {
            case 0: Offsets = WorldCache.VolumeCascadeGridCoordOffsets[0]; break;
            case 1: Offsets = WorldCache.VolumeCascadeGridCoordOffsets[1]; break;
            case 2: Offsets = WorldCache.VolumeCascadeGridCoordOffsets[2]; break;
            case 3: Offsets = WorldCache.VolumeCascadeGridCoordOffsets[3]; break;
        }
    }
    return (GridCoords.xyz + Offsets.xyz) * (1u << GridCoords.w);
}

float3 WorldCache_GetProbeWorldCenterFromGridCoords (int4 GridCoords, bool bPrevious = false) {
    return (bPrevious ? WorldCache.PreviousVolumeMin : WorldCache.VolumeMin) +
        WorldCache_GetAbsoluteGridCoords(GridCoords, bPrevious) * WorldCache.GridSize;
}

bool WorldCache_IsProbeCoordsOutOfBounds (int4 GridCoords) {
    if(any(GridCoords.xyz) < 0 || any(GridCoords.xyz >= WorldCache.GridCoordsBound)) {
        return true;
    }
    return false;
}

bool WorldCache_IsProbeCoordsOverlapped (int4 GridCoords) {
    // Check if it's overlapped with finer level
    if(GridCoords.w > 0) {
        int3 LowerLevelCoords = 
            2 * (GridCoords.xyz + WorldCache.VolumeCascadeGridCoordOffsets[GridCoords.w].xyz) 
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

void WorldCache_WriteProbeGridInternalLocation (int ProbeIndex, float3 Offsets) {
    g_RWWorldCacheProbeHeaderBuffer[ProbeIndex].y = asuint(Offsets.x);
    g_RWWorldCacheProbeHeaderBuffer[ProbeIndex].z = asuint(Offsets.y);
    g_RWWorldCacheProbeHeaderBuffer[ProbeIndex].w = asuint(Offsets.z);
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
        + Header.GridInternalNormalizedPosition * WorldCache.HalfGridSize * (1u << Header.GridCoords.w);
    return Header;
}

uint4 WorldCache_PackProbeHeader (WorldCacheProbeHeader Header) {
    uint4 Packed;
    // Decode grid coords & scores from a single uint (x:8, y:8, z:8, level:3, score:5)
    Packed.x = Header.GridCoords.x & 0xff;
    Packed.x |= (Header.GridCoords.y & 0xff) << 8;
    Packed.x |= (Header.GridCoords.z & 0xff) << 16;
    Packed.x |= (Header.GridCoords.w & 0x7) << 24;
    Packed.x |= uint(Header.Score) << 27;
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
int4 WorldCache_GetProbeGridCoordsBase (float3 Position, bool bPrevious = false) {
    float3 LocalPosition = Position - (bPrevious ? WorldCache.PreviousVolumeMin : WorldCache.VolumeMin);
    float3 AbsoluteGridPosition  = LocalPosition * WorldCache.GridSizeInv;
    int ClipmapLevel = WorldCache.MaxClipmapCascades;
    // fine-to-coarse grain search
    float3 LevelGridPosition = -1;
    [unroll(MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES)]
    for(int i = 0; i < WorldCache.MaxClipmapCascades; i++) {
        LevelGridPosition = AbsoluteGridPosition * (1.f / (1u << i)) - 
            (bPrevious ? WorldCache.PreviousVolumeCascadeGridCoordOffsets[i].xyz
                : WorldCache.VolumeCascadeGridCoordOffsets[i].xyz);
        if(all(LevelGridPosition >= 0)
        && all(LevelGridPosition < (WorldCache.GridCoordsBound - 1))) {
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

float4 WorldCache_GetProbeGridCoordsFp (float3 Position, bool bPrevious = false) {
    float3 LocalPosition = Position - (bPrevious ? WorldCache.PreviousVolumeMin : WorldCache.VolumeMin);
    float3 AbsoluteGridPosition  = LocalPosition * WorldCache.GridSizeInv;
    int ClipmapLevel = WorldCache.MaxClipmapCascades;
    // fine-to-coarse grain search
    float3 LevelGridPosition = -1;
    [unroll(MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES)]
    for(int i = 0; i < WorldCache.MaxClipmapCascades; i++) {
        LevelGridPosition = AbsoluteGridPosition * (1.f / (1u << i)) - 
            (bPrevious ? WorldCache.PreviousVolumeCascadeGridCoordOffsets[i].xyz
                : WorldCache.VolumeCascadeGridCoordOffsets[i].xyz);
        if(all(LevelGridPosition >= 0)
        && all(LevelGridPosition < (WorldCache.GridCoordsBound - 1))) {
            ClipmapLevel = i;
            break;
        }
    }
    return float4(
        LevelGridPosition.x,
        LevelGridPosition.y,
        LevelGridPosition.z,
        ClipmapLevel
    );
}

float WorldCache_GetSampleOffsetAmount (float3 WorldPosition) {
    float4 Coords = WorldCache_GetProbeGridCoordsFp(WorldPosition);
    float Size = WorldCache.GridSize;;
    if(Coords.w > 0) {
        float3 LowerLevelCoords = (Coords.xyz + WorldCache.VolumeCascadeGridCoordOffsets[Coords.w].xyz) * 2
            - WorldCache.VolumeCascadeGridCoordOffsets[Coords.w - 1].xyz;
        float MaxDist = max(hmax(- LowerLevelCoords), hmax(LowerLevelCoords - WorldCache.GridCoordsBound + 1));
        // 2 grids smooth transition
        Size = WorldCache_GetGridSizeForLevel(Coords.w) * lerp(0.5, 1, saturate(MaxDist / 2));
    }
    return Size * 0.2;//WorldCache.SampleBias;
} 

void WorldCache_RecycleProbe (int ProbeIndex) {
    int Count;
    InterlockedAdd(g_RWWorldCacheActiveProbeCountBuffer[0], -1, Count);
    int FreeProbeListIndex = WorldCache.NumProbes - Count;
    g_RWWorldCacheFreeProbeIndexBuffer[FreeProbeListIndex] = ProbeIndex;
}

uint WorldCache_AllocateProbe () {
    int Count;
    InterlockedAdd(g_RWWorldCacheActiveProbeCountBuffer[0], 1, Count);
    int FreeProbeListIndex = WorldCache.NumProbes - Count - 1;
    if(FreeProbeListIndex < 0) {
        return 0xffffffffu;
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
    return (Request.GridCoords.x & 0xff) |
        (Request.GridCoords.y & 0xff) << 8 |
        (Request.GridCoords.z & 0xff) << 16 |
        (Request.GridCoords.w & 0x7) << 24;
}

WorldCacheProbeSpawnRequest WorldCache_GetProbeSpawnRequest (int RequestIndex) {
    return WorldCache_UnpackProbeSpawnRequest(g_RWWorldCacheProbeSpawnRequestBuffer[RequestIndex]);
}

void WorldCache_AddProbeSpawnRequest (WorldCacheProbeSpawnRequest Request) {
    int RequestIndex;
    InterlockedAdd(g_RWWorldCacheProbeSpawnRequestCountBuffer[0], 1, RequestIndex);
    g_RWWorldCacheProbeSpawnRequestBuffer[RequestIndex] = WorldCache_PackProbeSpawnRequest(Request);
}

uint WorldCache_GetProbeIndexFromGrid (int4 GridCoords) {
    switch(GridCoords.w) {
        case 0: return g_RWWorldCacheProbeIndexTexture[0][GridCoords.xyz];
        case 1: return g_RWWorldCacheProbeIndexTexture[1][GridCoords.xyz];
        case 2: return g_RWWorldCacheProbeIndexTexture[2][GridCoords.xyz];
        case 3: return g_RWWorldCacheProbeIndexTexture[3][GridCoords.xyz];
    }
    return 0xffffffffu;
}

void WorldCache_WriteProbeIndexToGrid (int4 GridCoords, uint ProbeIndex) {
    switch(GridCoords.w) {
        case 0: g_RWWorldCacheProbeIndexTexture[0][GridCoords.xyz] = ProbeIndex; break;
        case 1: g_RWWorldCacheProbeIndexTexture[1][GridCoords.xyz] = ProbeIndex; break;
        case 2: g_RWWorldCacheProbeIndexTexture[2][GridCoords.xyz] = ProbeIndex; break;
        case 3: g_RWWorldCacheProbeIndexTexture[3][GridCoords.xyz] = ProbeIndex; break;
    }
}

int2 WorldCache_GetProbeAtlasCoords (int ProbeIndex) {
    int2 ProbeCoords = int2(
        ProbeIndex % WorldCache.ProbeAtlasWidth,
        ProbeIndex / WorldCache.ProbeAtlasWidth
    );
    return ProbeCoords;
}

int2 WorldCache_GetProbeAtlasBase (int ProbeIndex) {
    int2 ProbeCoords = WorldCache_GetProbeAtlasCoords(ProbeIndex);
    return ProbeCoords * WORLD_CACHE_PROBE_RESOLUTION;
}

struct WorldCacheSample {
    uint ProbeIndex[8];
    float Weights[8];
};

int4 WorldCache_GetLower(int4 GridCoords) {
    return int4(
        (GridCoords.xyz + WorldCache.VolumeCascadeGridCoordOffsets[GridCoords.w].xyz) * 2
         - WorldCache.VolumeCascadeGridCoordOffsets[GridCoords.w - 1].xyz,
         GridCoords.w - 1);
}

// Logic mostly comes from RTXGI-DDGI
WorldCacheSample WorldCache_SampleProbes (
    float3 WorldPosition,
    float3 WorldPositionBiased,
    float3 WorldNormal,
    float  RayTravelDistance,
    bool bPrevious = false,
    bool bNoNormal = false,
    bool bRequestProbeSpawn = false
) {
    WorldCacheSample Sample = (WorldCacheSample)0;
    for(int i = 0; i<8; i++) {
        Sample.ProbeIndex[i] = 0xffffffffu;
        Sample.Weights[i] = 0;
    }
    int4 GridCoordsBase = WorldCache_GetProbeGridCoordsBase(WorldPositionBiased, bPrevious);
    if(WorldCache_IsProbeCoordsOutOfBounds(GridCoordsBase)) {
        return Sample;
    }
    float3 Trillinear = 
        saturate((WorldPositionBiased - WorldCache_GetProbeWorldCenterFromGridCoords(GridCoordsBase, bPrevious))
         / WorldCache_GetGridSizeForLevel(GridCoordsBase.w));
    float SumWeight = 0;
    for(int i = 0; i < 8; i++) {
        int3 GridCoordsOffset = int3(
            i & 1, (i >> 1) & 1, (i >> 2) & 1
        );
        int4 GridCoords = GridCoordsBase + int4(GridCoordsOffset, 0);
        uint ProbeIndex = 0xffffffffu;
        if(!WorldCache_IsProbeCoordsOutOfBounds(GridCoords)) {
            ProbeIndex = WorldCache_GetProbeIndexFromGrid(GridCoords);
            // Try to spawn a probe if it's empty & valid
            if(bRequestProbeSpawn 
                && ProbeIndex == 0xffffffffu) {
                int4 SpawnGridCoords = GridCoords;
                // Try to spawn on lower levels if ever possible
                [unroll(MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES - 1)]
                while(SpawnGridCoords.w > 0 && WorldCache_IsProbeCoordsOverlapped(SpawnGridCoords)) {
                    int4 LowerGridCoords = WorldCache_GetLower(SpawnGridCoords);
                    SpawnGridCoords = LowerGridCoords;
                }
                {
                    uint OldValue;
                    InterlockedCompareExchange(
                        g_RWWorldCacheProbeIndexTexture[SpawnGridCoords.w][SpawnGridCoords.xyz],
                        0xffffffffu, 0xfffffffeu, OldValue
                    );
                    if(OldValue == 0xffffffffu) {
                        WorldCacheProbeSpawnRequest Request;
                        Request.GridCoords = GridCoords;
                        WorldCache_AddProbeSpawnRequest(Request);
                    }
                }
            }
        }
        if(!WorldCache_IsProbeIndexValid(ProbeIndex)) {
            continue;
        }
        float3 ProbePosition = WorldCache_GetProbeHeader(ProbeIndex).WorldPosition;
        float3 PosToProbeN = normalize(ProbePosition - WorldPosition);
        float3 BiasedPosToAdjProbeN = normalize(ProbePosition - WorldPositionBiased);
        float LinearWeight = 
            (GridCoordsOffset.x ? Trillinear.x : 1 - Trillinear.x) *
            (GridCoordsOffset.y ? Trillinear.y : 1 - Trillinear.y) *
            (GridCoordsOffset.z ? Trillinear.z : 1 - Trillinear.z);
        float Weight = 1.f;
        float WrapShading = (dot(PosToProbeN, WorldNormal) + 1.f) * 0.5f;
        if(!bNoNormal) Weight *= (WrapShading * WrapShading * WrapShading) + 0.05f;
        float2 MomentumOct01 = UnitVectorToOctahedron01(-BiasedPosToAdjProbeN);
        int2 ProbeBaseCoords = WorldCache_GetProbeAtlasBase(ProbeIndex);
        float2 MomentumTexPosition = 
            ProbeBaseCoords + 1 + MomentumOct01 * WORLD_CACHE_PROBE_RESOLUTION_INTERNAL;
        float2 MomentumAtlasUV = MomentumTexPosition * WorldCache.InvAtlasDimensions;
        float2 Momentum = g_WorldCacheMomentumTexture.SampleLevel(
            g_LinearSampler, MomentumAtlasUV, 0
        ).xy;
        // Clamp to 0 in case y < x*x due to approximations
        float Variance = max(0.00001f, (Momentum.x * Momentum.x) - Momentum.y);
        
        // Occlusion test
        float ChebyshevWeight = 1.f;
        float BiasedPosToAdjProbeDist = length(ProbePosition - WorldPositionBiased);
        if(BiasedPosToAdjProbeDist > Momentum.x) // occluded
        {
            // v must be greater than 0, which is guaranteed by the if condition above.
            float v = BiasedPosToAdjProbeDist - Momentum.x;
            ChebyshevWeight = Variance / (Variance + (v * v));
            // Increase the contrast in the weight
            ChebyshevWeight = max((ChebyshevWeight * ChebyshevWeight * ChebyshevWeight), 0.f);
        }
        // Make sure we have a fallback value if all probes are occluded.
        Weight *= max(0.001f, ChebyshevWeight);
        float ClampWeight = 1.f;
        {
            float Sgn = RayTravelDistance / max(length(ProbePosition - WorldPosition), 1e-6f);
            ClampWeight = smoothstep(0.9f, 1.2f, Sgn);
        }
        // Avoid leaking
        Weight *= ClampWeight;
        // Does this make sense?
        Weight = max(Weight, 1e-6f);

        // A small amount of light is visible due to logarithmic perception, so
        // crush tiny weights but keep the curve continuous
        const float CrushThreshold = 0.2f;
        if (Weight < CrushThreshold)
        {
            Weight *= (Weight * Weight) * (1.f / (CrushThreshold * CrushThreshold));
        }
        // Apply the trilinear weights
        Weight *= LinearWeight;

        Sample.ProbeIndex[i] = ProbeIndex;
        Sample.Weights[i] = Weight;
        SumWeight += Weight;
    }
    SumWeight = max(SumWeight, 1e-6f);
    const float FlattenThreshold = 0.1f;
    // Drop the sample if the weight is too small
    if(SumWeight < FlattenThreshold) {
        SumWeight = FlattenThreshold * sqrt((1.f + SumWeight / FlattenThreshold) * 0.5f);
    }
    for(int i = 0; i < 8; i++) {
        Sample.Weights[i] /= SumWeight;
    }
    return Sample;
}

void WorldCache_TouchSample (WorldCacheSample Sample) {
    for(int i = 0; i < 8; i++) {
        if(WorldCache_IsProbeIndexValid(Sample.ProbeIndex[i]) && Sample.Weights[i] > 0.01f) {
            g_RWWorldCacheProbeTouchBuffer[Sample.ProbeIndex[i]] = 1;
        }
    }
}

#endif // MIGI_WORLDCACHE_HLSL