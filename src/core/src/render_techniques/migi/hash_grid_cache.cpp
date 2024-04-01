/*
 * Project Capsaicin: hash_grid_cache.cpp
 * Created: 2024/3/28
 * This program uses MulanPSL2. See LICENSE for more.
 */
#include "capsaicin_internal.h"
#include "hash_grid_cache.h"

namespace Capsaicin
{
HashGridCache::HashGridCache(const GfxContext & gfx)
    : gfx_(gfx)
    , max_ray_count_(0)
    , num_cells_(0)
    , num_tiles_(0)
    , num_tiles_per_bucket_(0)
    , size_tile_mip0_(0)
    , size_tile_mip1_(0)
    , size_tile_mip2_(0)
    , size_tile_mip3_(0)
    , num_cells_per_tile_mip0_(0)
    , num_cells_per_tile_mip1_(0)
    , num_cells_per_tile_mip2_(0)
    , num_cells_per_tile_mip3_(0)
    , num_cells_per_tile_(0)
    , first_cell_offset_tile_mip0_(0)
    , first_cell_offset_tile_mip1_(0)
    , first_cell_offset_tile_mip2_(0)
    , first_cell_offset_tile_mip3_(0)
    , radiance_cache_hash_buffer_ping_pong_(0)
    , radiance_cache_hash_buffer_(radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_HASHBUFFER])
    , radiance_cache_decay_cell_buffer_(radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_DECAYCELLBUFFER])
    , radiance_cache_decay_tile_buffer_(radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_DECAYTILEBUFFER])
    , radiance_cache_value_buffer_(radiance_cache_hash_buffer_uint2_[HASHGRIDCACHE_VALUEBUFFER])
    , radiance_cache_update_tile_buffer_(radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_UPDATETILEBUFFER])
    , radiance_cache_update_tile_count_buffer_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_UPDATETILECOUNTBUFFER])
    , radiance_cache_update_cell_value_buffer_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_UPDATECELLVALUEBUFFER])
    , radiance_cache_visibility_buffer_(radiance_cache_hash_buffer_float4_[HASHGRIDCACHE_VISIBILITYBUFFER])
    , radiance_cache_visibility_count_buffer_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_VISIBILITYCOUNTBUFFER])
    , radiance_cache_visibility_cell_buffer_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_VISIBILITYCELLBUFFER])
    , radiance_cache_visibility_query_buffer_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_VISIBILITYQUERYBUFFER])
    , radiance_cache_visibility_ray_buffer_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_VISIBILITYRAYBUFFER])
    , radiance_cache_visibility_ray_count_buffer_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_VISIBILITYRAYCOUNTBUFFER])
    , radiance_cache_packed_tile_count_buffer0_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_PACKEDTILECOUNTBUFFER0])
    , radiance_cache_packed_tile_count_buffer1_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_PACKEDTILECOUNTBUFFER1])
    , radiance_cache_packed_tile_index_buffer0_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_PACKEDTILEINDEXBUFFER0])
    , radiance_cache_packed_tile_index_buffer1_(
          radiance_cache_hash_buffer_uint_[HASHGRIDCACHE_PACKEDTILEINDEXBUFFER1])
    , radiance_cache_debug_cell_buffer_(radiance_cache_hash_buffer_float4_[HASHGRIDCACHE_DEBUGCELLBUFFER])
{}

HashGridCache::~HashGridCache()
{
    for (GfxBuffer buffer : radiance_cache_hash_buffer_uint_)
    {
        gfxDestroyBuffer(gfx_, buffer);
    }

    for (GfxBuffer buffer : radiance_cache_hash_buffer_uint2_)
    {
        gfxDestroyBuffer(gfx_, buffer);
    }

    for (GfxBuffer buffer : radiance_cache_hash_buffer_float4_)
    {
        gfxDestroyBuffer(gfx_, buffer);
    }
}

void HashGridCache::ensureMemoryIsAllocated(const MIGIRenderOptions &options)
{

    uint32_t const max_ray_count        = options.max_SSRC_update_ray_count;
    uint32_t const num_buckets          = 1u << options.hash_grid_cache.num_buckets_l2;
    uint32_t const num_tiles_per_bucket = 1u << options.hash_grid_cache.num_tiles_per_bucket_l2;
    uint32_t const size_tile_mip0       = options.hash_grid_cache.tile_cell_ratio;
    uint32_t const size_tile_mip1       = size_tile_mip0 >> 1;
    uint32_t const size_tile_mip2       = size_tile_mip1 >> 1;
    uint32_t const size_tile_mip3       = size_tile_mip2 >> 1;
    uint32_t const size_tile_mip4       = size_tile_mip3 >> 1;
    GFX_ASSERT(size_tile_mip4 == 0);
    uint32_t const num_cells_per_tile_mip0 = size_tile_mip0 * size_tile_mip0;
    uint32_t const num_cells_per_tile_mip1 = size_tile_mip1 * size_tile_mip1;
    uint32_t const num_cells_per_tile_mip2 = size_tile_mip2 * size_tile_mip2;
    uint32_t const num_cells_per_tile_mip3 = size_tile_mip3 * size_tile_mip3;
    uint32_t const num_cells_per_tile =
        num_cells_per_tile_mip0 + num_cells_per_tile_mip1 + num_cells_per_tile_mip2 + num_cells_per_tile_mip3;
    uint32_t const first_cell_offset_tile_mip0 = 0;
    uint32_t const first_cell_offset_tile_mip1 = first_cell_offset_tile_mip0 + num_cells_per_tile_mip0;
    uint32_t const first_cell_offset_tile_mip2 = first_cell_offset_tile_mip1 + num_cells_per_tile_mip1;
    uint32_t const first_cell_offset_tile_mip3 = first_cell_offset_tile_mip2 + num_cells_per_tile_mip2;
    GFX_ASSERT(first_cell_offset_tile_mip3 + num_cells_per_tile_mip3 == num_cells_per_tile);
    uint32_t const num_tiles = num_tiles_per_bucket * num_buckets;
    uint32_t const num_cells = num_cells_per_tile * num_tiles;

    if (!radiance_cache_hash_buffer_ || num_tiles != num_tiles_)
    {
        gfxDestroyBuffer(gfx_, radiance_cache_hash_buffer_);
        gfxDestroyBuffer(gfx_, radiance_cache_decay_tile_buffer_);

        radiance_cache_hash_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, num_tiles);
        radiance_cache_hash_buffer_.setName("Capsaicin_RadianceCache_HashBuffer");

        radiance_cache_decay_tile_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, num_tiles);
        radiance_cache_decay_tile_buffer_.setName("Capsaicin_RadianceCache_DecayTileBuffer");

        gfxCommandClearBuffer(gfx_, radiance_cache_hash_buffer_); // clear the radiance cache
    }

    if (!radiance_cache_value_buffer_ || num_cells != num_cells_)
    {
        gfxDestroyBuffer(gfx_, radiance_cache_value_buffer_);

        radiance_cache_value_buffer_ = gfxCreateBuffer<uint2>(gfx_, num_cells);
        radiance_cache_value_buffer_.setName("Capsaicin_RadianceCache_ValueBuffer");
    }

    if (!radiance_cache_update_tile_count_buffer_)
    {
        radiance_cache_update_tile_count_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, 1);
        radiance_cache_update_tile_count_buffer_.setName("Capsaicin_RadianceCache_UpdateTileCountBuffer");
    }

    if (!radiance_cache_update_cell_value_buffer_ || num_cells != num_cells_)
    {
        gfxDestroyBuffer(gfx_, radiance_cache_update_cell_value_buffer_);

        radiance_cache_update_cell_value_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, num_cells << 2);
        radiance_cache_update_cell_value_buffer_.setName("Capsaicin_RadianceCache_UpdateCellValueBuffer");

        gfxCommandClearBuffer(gfx_, radiance_cache_update_cell_value_buffer_);
    }

    if (!radiance_cache_visibility_count_buffer_)
    {
        radiance_cache_visibility_count_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, 1);
        radiance_cache_visibility_count_buffer_.setName("Capsaicin_RadianceCache_VisibilityCountBuffer");

        radiance_cache_visibility_ray_count_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, 1);
        radiance_cache_visibility_ray_count_buffer_.setName(
            "Capsaicin_RadianceCache_VisibilityRayCountBuffer");

        radiance_cache_packed_tile_count_buffer0_ = gfxCreateBuffer<uint32_t>(gfx_, 1);
        radiance_cache_packed_tile_count_buffer0_.setName("Capsaicin_RadianceCache_PackedTileCountBuffer0");

        radiance_cache_packed_tile_count_buffer1_ = gfxCreateBuffer<uint32_t>(gfx_, 1);
        radiance_cache_packed_tile_count_buffer1_.setName("Capsaicin_RadianceCache_PackedTileCountBuffer1");
    }

    if (!radiance_cache_packed_tile_index_buffer0_ || num_tiles != num_tiles_)
    {
        gfxDestroyBuffer(gfx_, radiance_cache_packed_tile_index_buffer0_);
        gfxDestroyBuffer(gfx_, radiance_cache_packed_tile_index_buffer1_);

        radiance_cache_packed_tile_index_buffer0_ = gfxCreateBuffer<uint32_t>(gfx_, num_tiles);
        radiance_cache_packed_tile_index_buffer0_.setName("Capsaicin_RadianceCache_PackedTileIndexBuffer0");

        radiance_cache_packed_tile_index_buffer1_ = gfxCreateBuffer<uint32_t>(gfx_, num_tiles);
        radiance_cache_packed_tile_index_buffer1_.setName("Capsaicin_RadianceCache_PackedTileIndexBuffer1");

        gfxCommandClearBuffer(gfx_, radiance_cache_packed_tile_index_buffer0_);
        gfxCommandClearBuffer(gfx_, radiance_cache_packed_tile_index_buffer1_);
    }

    // The `packedCell' buffer is not necessary for drawing, but rather used
    // when debugging our hash cells.
    // So, we only allocate the memory when debugging the hash grid radiance
    // cache, and release it when not.
    if (options.active_debug_view.starts_with("HashGridCache_"))
    {
        if (!radiance_cache_debug_cell_buffer_ || num_cells != num_cells_)
        {
            gfxDestroyBuffer(gfx_, radiance_cache_debug_cell_buffer_);
            gfxDestroyBuffer(gfx_, radiance_cache_decay_cell_buffer_);

            radiance_cache_debug_cell_buffer_ = gfxCreateBuffer<float4>(gfx_, num_cells);
            radiance_cache_debug_cell_buffer_.setName("Capsaicin_RadianceCache_DebugCellBuffer");

            radiance_cache_decay_cell_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, num_cells);
            radiance_cache_decay_cell_buffer_.setName("Capsaicin_RadianceCache_DecayCellBuffer");

            gfxCommandClearBuffer(gfx_, radiance_cache_decay_cell_buffer_, 0xFFFFFFFFu);
        }
    }
    else
    {
        gfxDestroyBuffer(gfx_, radiance_cache_debug_cell_buffer_);
        gfxDestroyBuffer(gfx_, radiance_cache_decay_cell_buffer_);

        radiance_cache_debug_cell_buffer_ = {};
        radiance_cache_decay_cell_buffer_ = {};
    }

    if (!radiance_cache_update_tile_buffer_ || max_ray_count != max_ray_count_ || num_cells != num_cells_)
    {
        gfxDestroyBuffer(gfx_, radiance_cache_update_tile_buffer_);
        gfxDestroyBuffer(gfx_, radiance_cache_visibility_buffer_);
        gfxDestroyBuffer(gfx_, radiance_cache_visibility_cell_buffer_);
        gfxDestroyBuffer(gfx_, radiance_cache_visibility_query_buffer_);
        gfxDestroyBuffer(gfx_, radiance_cache_visibility_ray_buffer_);

        radiance_cache_update_tile_buffer_ =
            gfxCreateBuffer<uint32_t>(gfx_, GFX_MIN(max_ray_count, num_cells));
        radiance_cache_update_tile_buffer_.setName("Capsaicin_RadianceCache_UpdateTileBuffer");

        radiance_cache_visibility_buffer_ = gfxCreateBuffer<float4>(gfx_, max_ray_count);
        radiance_cache_visibility_buffer_.setName("Capsaicin_RadianceCache_VisibilityBuffer");

        radiance_cache_visibility_cell_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, max_ray_count);
        radiance_cache_visibility_cell_buffer_.setName("Capsaicin_RadianceCache_VisibilityCellBuffer");

        radiance_cache_visibility_query_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, max_ray_count);
        radiance_cache_visibility_query_buffer_.setName("Capsaicin_RadianceCache_VisibilityQueryBuffer");

        radiance_cache_visibility_ray_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, max_ray_count);
        radiance_cache_visibility_ray_buffer_.setName("Capsaicin_RadianceCache_VisibilityRayBuffer");
    }

    max_ray_count_               = max_ray_count;
    num_buckets_                 = num_buckets;
    num_tiles_                   = num_tiles;
    num_cells_                   = num_cells;
    num_tiles_per_bucket_        = num_tiles_per_bucket;
    size_tile_mip0_              = size_tile_mip0;
    size_tile_mip1_              = size_tile_mip1;
    size_tile_mip2_              = size_tile_mip2;
    size_tile_mip3_              = size_tile_mip3;
    num_cells_per_tile_mip0_     = num_cells_per_tile_mip0;
    num_cells_per_tile_mip1_     = num_cells_per_tile_mip1;
    num_cells_per_tile_mip2_     = num_cells_per_tile_mip2;
    num_cells_per_tile_mip3_     = num_cells_per_tile_mip3;
    num_cells_per_tile_          = num_cells_per_tile; // all mips
    first_cell_offset_tile_mip0_ = first_cell_offset_tile_mip0;
    first_cell_offset_tile_mip1_ = first_cell_offset_tile_mip1;
    first_cell_offset_tile_mip2_ = first_cell_offset_tile_mip2;
    first_cell_offset_tile_mip3_ = first_cell_offset_tile_mip3;
}

}