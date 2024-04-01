/*
 * Project Capsaicin: migi_fwd.h
 * Created: 2024/3/28
 * This program uses MulanPSL2. See LICENSE for more.
 */

#ifndef CAPSAICIN_MIGI_FWD_H
#define CAPSAICIN_MIGI_FWD_H
#include "capsaicin.h"
#include "migi_common.hlsl"

namespace Capsaicin {

struct MIGIRenderOptions {

    uint32_t width {};
    uint32_t height {};

    // 0: visualize bsdf, 1: visualize model, 2: training
    uint32_t visualize_mode = 0;

    bool reset_screen_space_cache {true};

    // Maximum number of update rays for the screen space radiance cache
    uint32_t max_SSRC_update_ray_count {};

    struct {
        uint32_t num_buckets_l2 {12}; // 1<<12 = 4096
        uint32_t num_tiles_per_bucket_l2 {4}; // 1<<4 = 16 tiles per bucket
        uint32_t tile_cell_ratio {8}; // 8x8 cells per tile
        float    cell_size {32.f};
        // 16 samples ensembles a stable result
        float max_sample_count {16.f};
        uint32_t debug_mip_level {};
        bool debug_propagate {false};
        uint32_t debug_max_cell_decay {50};
        HashGridCacheDebugMode debug_mode {HASHGRIDCACHE_DEBUG_RADIANCE};
    } hash_grid_cache ;

    struct {
        float    reservoir_cache_cell_size {16.f};
        // This parameter is set with the screen resolution during options update every frame.
        uint32_t max_query_ray_count {};
    } restir;
    glm::vec3 sg_direction = glm::vec3(0.0f, 1.0f, -1.0f);

    glm::vec3 sg_li_position = glm::vec3(0.0f, 0.0f, 0.0f);
    float     sg_lambda    = 40.f;
    glm::vec3 sg_color     = glm::vec3(0.5f, 0.5f, 0.5f);
    float     sg_intensity = 1;

    float roughness = 0.5f;

    bool no_importance_sampling = true;

    float lr_rate   = 0.01f;

    // Whether to use the channeled cache for SG lighting.
    bool channeled_cache = false;

    // Whether to shade with geometry normals only
    bool shading_with_geometry_normal {false};

    std::string active_debug_view {};
};

}

#endif // CAPSAICIN_MIGI_FWD_H
