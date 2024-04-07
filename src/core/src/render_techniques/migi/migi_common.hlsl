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

// 32 x 5 x 5 = 800 <= 1024 (max thread group size)
#define COOPERATIVE_SHADING_GRID_SIZE 5

#ifdef __cplusplus
}// namespace Capsaicin
#endif
#endif // MIGI_COMMON_HLSL