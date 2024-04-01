/*
 * Project Capsaicin: hash_grid_cache.h
 * Created: 2024/3/28
 * This program uses MulanPSL2. See LICENSE for more.
 */

#ifndef CAPSAICIN_HASH_GRID_CACHE_H
#define CAPSAICIN_HASH_GRID_CACHE_H

#include "capsaicin.h"
#include "migi_common.hlsl"
#include "migi_fwd.h"

namespace Capsaicin
{
// Used for caching in world space the lighting calculated at primary (same as screen probes) and
// secondary path vertices.
struct HashGridCache
{
    HashGridCache(const GfxContext & gfx_);
    ~HashGridCache();

    void ensureMemoryIsAllocated(MIGIRenderOptions const &options);

    uint32_t max_ray_count_;
    uint32_t num_buckets_;
    uint32_t num_tiles_;
    uint32_t num_cells_;
    uint32_t num_tiles_per_bucket_;
    uint32_t size_tile_mip0_;
    uint32_t size_tile_mip1_;
    uint32_t size_tile_mip2_;
    uint32_t size_tile_mip3_;
    uint32_t num_cells_per_tile_mip0_;
    uint32_t num_cells_per_tile_mip1_;
    uint32_t num_cells_per_tile_mip2_;
    uint32_t num_cells_per_tile_mip3_;
    uint32_t num_cells_per_tile_; // all mips
    uint32_t first_cell_offset_tile_mip0_;
    uint32_t first_cell_offset_tile_mip1_;
    uint32_t first_cell_offset_tile_mip2_;
    uint32_t first_cell_offset_tile_mip3_;

    GfxBuffer  radiance_cache_hash_buffer_uint_[HASHGRID_UINT_BUFFER_COUNT];
    GfxBuffer  radiance_cache_hash_buffer_uint2_[HASHGRID_UINT2_BUFFER_COUNT];
    GfxBuffer  radiance_cache_hash_buffer_float4_[HASHGRID_FLOAT4_BUFFER_COUNT];
    uint32_t   radiance_cache_hash_buffer_ping_pong_;
    GfxBuffer &radiance_cache_hash_buffer_;
    GfxBuffer &radiance_cache_decay_cell_buffer_;
    GfxBuffer &radiance_cache_decay_tile_buffer_;
    GfxBuffer &radiance_cache_value_buffer_;
    GfxBuffer &radiance_cache_update_tile_buffer_;
    GfxBuffer &radiance_cache_update_tile_count_buffer_;
    GfxBuffer &radiance_cache_update_cell_value_buffer_;
    GfxBuffer &radiance_cache_visibility_buffer_;
    GfxBuffer &radiance_cache_visibility_count_buffer_;
    GfxBuffer &radiance_cache_visibility_cell_buffer_;
    GfxBuffer &radiance_cache_visibility_query_buffer_;
    GfxBuffer &radiance_cache_visibility_ray_buffer_;
    GfxBuffer &radiance_cache_visibility_ray_count_buffer_;
    GfxBuffer &radiance_cache_packed_tile_count_buffer0_;
    GfxBuffer &radiance_cache_packed_tile_count_buffer1_;
    GfxBuffer &radiance_cache_packed_tile_index_buffer0_;
    GfxBuffer &radiance_cache_packed_tile_index_buffer1_;
    GfxBuffer &radiance_cache_debug_cell_buffer_;

    const GfxContext & gfx_;
};

}

#endif // CAPSAICIN_HASH_GRID_CACHE_H
