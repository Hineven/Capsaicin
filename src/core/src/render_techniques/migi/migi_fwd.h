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
    bool     reset_world_cache {false};

    bool     DDGI_final_gather {false};
    bool     show_SG_lighting_only {false};

    uint32_t SSRC_max_update_ray_count {4 * 1024 * 1024};
    uint32_t SSRC_max_adaptive_probe_count {32 * 1024};
    uint32_t SSRC_max_basis_count {4 * 1024 * 1024};
    uint32_t SSRC_max_probe_count {};
    uint32_t SSRC_base_update_ray_waves {2};

    struct {
        int max_query_count;  // reload
        float grid_size {0.15f}; // reset
        int   clipmap_resolution {32};  // reload
        int   clipmap_levels {4}; // reload

        int probe_initial_score {30}; // < 31
        int probe_score_decay {1};
        int probe_score_bonus {25};

        int max_probe_count          = 4 * 16384; // reload
        int num_update_ray_per_probe = 88;//148; // max 243

        float sample_bias                = 0.1f;
        float probe_irradiance_threshold = 0.2f;
        float probe_luminance_threshold  = 0.4f;
    } world_cache;

    // If we disable importance sampling when generate update rays.
    // When enabled, rays are uniformly sampled in the hemisphere.
    bool no_importance_sampling = false;
    // Whether to place adaptive probes
    bool no_adaptive_probes = false;
    // Disable the denoiser
    bool no_denoiser = false;
    // Disable spatial probe filtering
    bool no_probe_filtering = false;
    // Whether to render indirect lighting (using the hash grid cache)
    bool enable_indirect = true;
    // Whether to disable SGs for caching (use irradiance only)
    bool disable_SG = false;
    // Whether to use square weighted radiance for SG direction update
    bool SSRC_squared_SG_directional_weight = true;

    // Merging threshold required when doing frame-to-frame SG reprojection
    float SSRC_SG_merging_threshold = 0.5f;

    // Alpha for measuring SG similarities when comparing lambdas
    // Larger values make the weight smaller when lambda variates
    float SSRC_SG_similarity_alpha = 0.8f;

    // Accelerate learning SG lambdas
    float SSRC_SG_lambda_learning_bonus = 25.f;
    float SSRC_SG_color_learning_bonus   = 1.05f;
    float SSRC_SG_direction_learing_rate = 0.12f;

    bool ambient_occlusion = false;
    bool near_field_global_illumination = false;

    float cache_update_learing_rate = 0.02f;
    bool  cache_update_SG_color {true};
    bool  cache_update_SG_direction {true};
    bool  cache_update_SG_lambda {true};

    bool  always_export {false};

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

    bool Inspection_VisualizeProbe {true};
    bool Inspection_VisualizeRays {true};

    bool Inspection_SH {false};
    bool Inspection_SG {true};
    bool Inspection_Oct {true};

    int fixed_tile_jitter {123 % 8};
    int fixed_frame_seed {123};
};

namespace MIGIRT {
    static char const *kMIGICacheUpdateRaygenShaderName       = "MIGI_CacheUpdateRaygen";
    static char const *kMIGICacheUpdateMissShaderName         = "MIGI_CacheUpdateMiss";
    static char const *kMIGICacheUpdateAnyHitShaderName       = "MIGI_CacheUpdateAnyHit";
    static char const *kMIGICacheUpdateClosestHitShaderName   = "MIGI_CacheUpdateClosestHit";
    static char const *kMIGICacheUpdateHitGroupName           = "MIGI_CacheUpdateHitGroup";
}

constexpr uint32_t kExportBufferSize = 16 * 1024 * 1024;

}

#endif // CAPSAICIN_MIGI_FWD_H
