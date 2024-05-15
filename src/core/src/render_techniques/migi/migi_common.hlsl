#ifndef MIGI_COMMON_HLSL
#define MIGI_COMMON_HLSL

#include "../../gpu_shared.h"

// The float4x4 typedef will pollute the CUDA namespace.
#ifdef __CUDA_ARCH__
#error "This file is not compatiable with CUDA."
#endif
#ifdef __cplusplus
namespace Capsaicin {
#endif

enum HashGridCacheDebugMode
{
    HASHGRIDCACHE_DEBUG_RADIANCE,
    HASHGRIDCACHE_DEBUG_RADIANCE_SAMPLE_COUNT,
    HASHGRIDCACHE_DEBUG_FILTERED_RADIANCE,
    HASHGRIDCACHE_DEBUG_FILTERING_GAIN,
    HASHGRIDCACHE_DEBUG_FILTERED_SAMPLE_COUNT,
    HASHGRIDCACHE_DEBUG_FILTERED_MIP_LEVEL,
    HASHGRIDCACHE_DEBUG_TILE_OCCUPANCY
};

struct HashGridCacheConstants
{
    float                  cell_size;
    float                  min_cell_size;
    float                  tile_size;
    float                  tile_cell_ratio; // tile_size / cell_size
    uint                   num_buckets;
    uint                   num_tiles;
    uint                   num_cells;
    uint                   num_tiles_per_bucket;
    uint                   size_tile_mip0;
    uint                   size_tile_mip1;
    uint                   size_tile_mip2;
    uint                   size_tile_mip3;
    uint                   num_cells_per_tile_mip0;
    uint                   num_cells_per_tile_mip1;
    uint                   num_cells_per_tile_mip2;
    uint                   num_cells_per_tile_mip3;
    uint                   num_cells_per_tile; // sum all available mips
    uint                   first_cell_offset_tile_mip0;
    uint                   first_cell_offset_tile_mip1;
    uint                   first_cell_offset_tile_mip2;
    uint                   first_cell_offset_tile_mip3;
    uint                   buffer_ping_pong;
    float                  max_sample_count;
    uint                   debug_mip_level;
    uint                   debug_propagate;
    uint                   debug_max_cell_decay;
    uint                   debug_bucket_occupancy_histogram_size;
    uint                   debug_bucket_overflow_histogram_size;
    HashGridCacheDebugMode debug_mode;
};

enum HashGridBufferNamesFloat
{
    HASHGRIDCACHE_STATSBUFFER,
    HASHGRID_FLOAT_BUFFER_COUNT
};

enum HashGridBufferNamesUint
{
    HASHGRIDCACHE_HASHBUFFER = 0,
    HASHGRIDCACHE_DECAYCELLBUFFER,
    HASHGRIDCACHE_DECAYTILEBUFFER,
    HASHGRIDCACHE_UPDATETILEBUFFER,
    HASHGRIDCACHE_UPDATETILECOUNTBUFFER,
    HASHGRIDCACHE_UPDATECELLVALUEBUFFER,
    HASHGRIDCACHE_VISIBILITYCOUNTBUFFER,
    HASHGRIDCACHE_VISIBILITYCELLBUFFER,
    HASHGRIDCACHE_VISIBILITYQUERYBUFFER,
    HASHGRIDCACHE_VISIBILITYRAYBUFFER,
    HASHGRIDCACHE_VISIBILITYRAYCOUNTBUFFER,
    HASHGRIDCACHE_PACKEDTILECOUNTBUFFER0,
    HASHGRIDCACHE_PACKEDTILECOUNTBUFFER1,
    HASHGRIDCACHE_PACKEDTILEINDEXBUFFER0,
    HASHGRIDCACHE_PACKEDTILEINDEXBUFFER1,
    HASHGRIDCACHE_BUCKETOCCUPANCYBUFFER,
    HASHGRIDCACHE_BUCKETOVERFLOWCOUNTBUFFER,
    HASHGRIDCACHE_BUCKETOVERFLOWBUFFER,
    HASHGRIDCACHE_FREEBUCKETBUFFER,
    HASHGRIDCACHE_USEDBUCKETBUFFER,
    HASHGRID_UINT_BUFFER_COUNT
};

enum HashGridBufferNamesUint2
{
    HASHGRIDCACHE_VALUEBUFFER = 0,
    HASHGRID_UINT2_BUFFER_COUNT
};

enum HashGridBufferNamesFloat4
{
    HASHGRIDCACHE_VISIBILITYBUFFER = 0,
    HASHGRIDCACHE_DEBUGCELLBUFFER,
    HASHGRID_FLOAT4_BUFFER_COUNT
};

struct WorldSpaceReSTIRConstants
{
    float cell_size;
    uint  num_cells;
    uint  num_entries_per_cell;
    uint  unused_padding;
};

struct RTConstants
{
    GpuVirtualAddressRange          ray_generation_shader_record;
    GpuVirtualAddressRangeAndStride miss_shader_table;
    uint2                           padding0;
    GpuVirtualAddressRangeAndStride hit_group_table;
    uint2                           padding1;
    GpuVirtualAddressRangeAndStride callable_shader_table;
    uint2                           padding2;
};

struct ProbeHeader {
    // Screen pixel position of the probe
    int2 ScreenPosition;
    int BasisOffset;
    // 0: 1, 1: 2, 2: 4, 3: 8, 4: 12
    int  Rank;
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

struct MIGI_Constants {
    
    // Common view parameters
    float3   CameraPosition;
    float3   CameraDirection;
    float    CameraFoVY;
    float    CameraFoVY2;
    float    AspectRatio;
    float    CameraNear;
    float    CameraFar;
    float3   CameraUp;
    float3   CameraRight;
    float4x4 CameraView;
    float4x4 CameraProjView;
    float4x4 CameraViewInv;
    float4x4 CameraProjViewInv;
    // The scale of a single pixel in the standard camera plane (z = 1)
    float    CameraPixelScale;

    // Note: NDC in MIGI means [-1, 1] x [-1, 1] x [0, 1], and 1 stands for the far plane.
    // Current NDC -> Prev NDC
    float4x4 Reprojection;
    // Prev    NDC -> Current NDC
    float4x4 ForwardProjection;

    float3   PreviousCameraPosition;

    uint FrameIndex;
    // Normally this is the same as FrameIndex, used for random number generation
    uint FrameSeed; 
    uint PreviousFrameSeed;

    // Screen resolution
    int2   ScreenDimensions;
    float2 ScreenDimensionsInv;
    // Screen resolution, counted in tiles
    int2   TileDimensions;
    float2 TileDimensionsInv;
    // Number of tiles / uniform screen probes
    int    UniformScreenProbeCount;

    // Budget for update rays
    int    UpdateRayBudget; 
    // Pad the fraction for ray allocation among probes, 0: error propotional, 1: avg.
    float  UpdateRayFractionPadding;

    // SSRC parameters
    // Maximum number of adaptive probes to allocate
    int MaxAdaptiveProbeCount;

    // Misc parameters
    uint NoImportanceSampling;
    uint NoAdaptiveProbes;

    // Learing parameters
    float CacheUpdateLearningRate;
    uint  CacheUpdate_SGColor;
    uint  CacheUpdate_SGDirection;
    uint  CacheUpdate_SGLambda;

    uint  DebugVisualizeMode;
    uint  DebugVisualizeChannel;
    uint  DebugVisualizeIncidentRadianceNumPoints;

    // Replace FrameSeed under certain conditions
    uint  DebugFreezeFrameSeed;
    uint  DebugFreezeFrameSeedValue;

    float DebugTonemapExposure;
    uint2 DebugCursorPixelCoords;

    // Used for single virtual emitter debugging
    uint   DebugLight;
    float3 DebugLightPosition;
    float  DebugLightSize;
    float3 DebugLightColor;
};


// The screen tile size (in pixels) 
#define SSRC_TILE_SIZE 16
#define SSRC_TILE_SIZE_L2 4
#ifdef __cplusplus
static_assert((1 << SSRC_TILE_SIZE_L2) == SSRC_TILE_SIZE, "SSRC_TILE_SIZE != 1<<SSRC_TILE_SIZE_L2.");
#endif
#if SSRC_TILE_SIZE != (1 << SSRC_TILE_SIZE_L2)
#error "SSRC_TILE_SIZE != 1<<SSRC_TILE_SIZE_L2."
#endif

#define SSRC_MAX_NUM_BASIS_PER_PROBE 8

#ifdef __cplusplus
}// namespace Capsaicin
#endif
#endif // MIGI_COMMON_HLSL