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
    bool     debug_freeze_tile_jitter {false};

    bool     reset_screen_space_cache {false};

    uint32_t SSRC_max_update_ray_count {4 * 1024 * 1024};
    uint32_t SSRC_max_adaptive_probe_count {32 * 1024};
    uint32_t SSRC_max_basis_count {4 * 1024 * 1024};
    uint32_t SSRC_max_probe_count {};

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
    bool no_importance_sampling = false;
    // Whether to place adaptive probes
    bool no_adaptive_probes = false;
    // Disable the denoiser
    bool no_denoiser = false;
    // Whether to render indirect lighting (using the hash grid cache)
    bool enable_indirect = true;
    // Whether to disable SGs for caching (use irradiance only)
    bool disable_SG = false;

    bool ambient_occlusion = true;
    bool near_field_global_illumination = true;

    float cache_update_learing_rate = 0.02f;
    bool  cache_update_SG_color {true};
    bool  cache_update_SG_direction {true};
    bool  cache_update_SG_lambda {true};

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

    int fixed_tile_jitter {123 % 8};
    int fixed_frame_seed {123};
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
