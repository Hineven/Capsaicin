#ifndef MIGI_COMMON_HLSL
#define MIGI_COMMON_HLSL

#ifdef GI10_SHARED_H
#error "migi_common.hlsl can not be co-included with gi10_shared.h"
#endif

// The float4x4 typedef will pollute the CUDA namespace.
#ifndef __CUDA_ARCH__
#ifdef __cplusplus
#include <cstdint>
#include <glm/glm.hpp>
typedef uint32_t uint;
typedef glm::mat4x4 float4x4;
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
    HashGridCacheDebugMode debug_mode;
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

#endif // __CUDA_ARCH__
#endif // MIGI_COMMON_HLSL