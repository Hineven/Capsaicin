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

    bool use_dxr10 {false};

    uint32_t width {};
    uint32_t height {};

    uint32_t debug_visualize_mode {};
    uint32_t debug_visualize_channel {};
    // Buffer recreation is required upon modifying this value
    uint32_t debug_visualize_incident_radiance_num_points {128 * 1024};

    bool     debug_freeze_frame_seed {false};

    bool reset_screen_space_cache {false};

    uint32_t SSRC_max_update_ray_count {4 * 1024 * 1024};
    // Maximum number of basis active in the screen space radiance cache
    uint32_t SSRC_max_basis_count {256 * 1024};
    // Min coverage for basis spawn
    float SSRC_basis_spawn_coverage_threshold {3.f};
    // Radius control for basis injection
    float SSRC_min_weight_E {0.08f};
    // Default initial W radius for newly generated basis
    float SSRC_initial_W_radius {9.f};
    // Resolution of the disk when doing rasterization for tile index injection
    uint32_t SSRC_CR_disk_vertex_count {12};

    // Ray budget for each frame
    uint32_t SSRC_update_ray_budget {2 * 1024 * 1024};
    // Used to pad W for cache coverage computation to prevent over allocation on surfaces parallel to the view direction
    float    SSRC_W_coverage_padding {0.05f};

    // Used to adjust the impact of importance sampling on ray allocation among tiles. 1: avg, 0: importance
    float    SSRC_tile_fraction_padding {0.2f};

    struct {
        uint32_t num_buckets_l2 {12}; // 1<<12 = 4096
        uint32_t num_tiles_per_bucket_l2 {4}; // 1<<4 = 16 tiles per bucket
        uint32_t tile_cell_ratio {8}; // 8x8 cells per tile
        float    cell_size {32.f};
        float    min_cell_size {1e-1f};
        // 16 samples ensembles a stable result
        float max_sample_count {16.f};
        uint32_t debug_mip_level {};
        bool debug_propagate {false};
        uint32_t debug_max_cell_decay {50};
        HashGridCacheDebugMode debug_mode {HASHGRIDCACHE_DEBUG_RADIANCE};

        int debug_max_bucket_overflow {64};
    } hash_grid_cache ;

    struct {
        float    reservoir_cache_cell_size {16.f};
        // This parameter is set with the screen resolution during options update every frame.
        uint32_t max_query_ray_count {};
    } restir;

    // If we disable importance sampling when generate update rays.
    // When enabled, rays are uniformly sampled in the hemisphere.
    bool no_importance_sampling = true;
    // If we use fixed step size in gradient descent.
    bool fixed_step_size = false;
    // Whether to render indirect lighting (using the hash grid cache)
    bool enable_indirect = true;
    // Whether to freeze the allocation and deallocation of basis for visualization
    bool freeze_basis_allocation {false};
    // Guess the initial W (basis radius) when generating new basis
    bool nonuniform_initial_w {false};

    float cache_update_learing_rate = 0.02f;
    bool  cache_update_SG_color {true};
    bool  cache_update_SG_direction {false};
    bool  cache_update_SG_lambda {false};
    bool  cache_update_W_alpha {false};
    bool  cache_update_W_lambda {false};

    // Whether to shade with geometry normals only
    bool shading_with_geometry_normal {false};

    std::string active_debug_view {};
    bool debug_view_switched {false};

    // Updated each frame
    int2 cursor_pixel_coords {};
    bool cursor_clicked {};
    bool cursor_dragging {};

    bool debug_light {};
    glm::vec3 debug_light_position {0.f, 1.f, 0.f};
    float debug_light_size {0.1f};
    glm::vec3 debug_light_color {1.f, 0.f, 0.f};
};

namespace MIGIRT {
    static char const *kScreenCacheUpdateRaygenShaderName     = "ScreenCacheUpdateRaygen";
    static char const *kScreenCacheUpdateMissShaderName       = "ScreenCacheUpdateMiss";
    static char const *kScreenCacheUpdateAnyHitShaderName     = "ScreenCacheUpdateAnyHit";
    static char const *kScreenCacheUpdateClosestHitShaderName = "ScreenCacheUpdateClosestHit";
    static char const *kScreenCacheUpdateHitGroupName         = "ScreenCacheUpdateHitGroup";

    static char const *kPopulateCellsRaygenShaderName     = "PopulateCellsRaygen";
    static char const *kPopulateCellsMissShaderName       = "PopulateCellsMiss";
    static char const *kPopulateCellsAnyHitShaderName     = "PopulateCellsAnyHit";
    static char const *kPopulateCellsClosestHitShaderName = "PopulateCellsClosestHit";
    static char const *kPopulateCellsHitGroupName         = "PopulateCellsHitGroup";
}

}

#endif // CAPSAICIN_MIGI_FWD_H
