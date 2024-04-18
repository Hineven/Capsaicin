/*
* Project Capsaicin: nn_trainer.h
* Created: 2024/1/3
* This program uses MulanPSL2. See LICENSE for more.
*/

#ifndef CAPSAICIN_MIGI_H
#define CAPSAICIN_MIGI_H

#include "hash_grid_cache.h"
#include "world_space_restir.h"
#include "migi_common.hlsl"
#include "render_technique.h"

namespace Capsaicin
{

class MIGI : public RenderTechnique
{
public:
    MIGI();
    ~MIGI();

    /*
        * Gets configuration options for current technique.
        * @return A list of all valid configuration options.
     */
    RenderOptionList getRenderOptions() noexcept override;

    bool init(const CapsaicinInternal &capsaicin) noexcept override;

    void terminate() noexcept override;

    void render(CapsaicinInternal &capsaicin) noexcept override;

    [[nodiscard]] AOVList getAOVs() const noexcept override;

    [[nodiscard]] DebugViewList getDebugViews() const noexcept override;

    [[nodiscard]] ComponentList getComponents() const noexcept override;

    [[nodiscard]] std::vector<std::string> getShaderCompileDefinitions (const CapsaicinInternal &) const ;

    struct Config {
        int wave_lane_count {};
        int basis_buffer_allocation {};
    } cfg_;
    bool initConfig (const CapsaicinInternal &capsaicin);

    struct MIGIResources
    {
        // Ray buffers for cache update
        // R16G16B16A16_FLOAT
        GfxTexture   update_ray_direction {};
        // R16G16B16A16_FLOAT
        GfxTexture   update_ray_radiance {};
        // RayRadiance - CachedRadiance, wsum
        // R16G16B16A16_FLOAT
        GfxTexture   update_ray_radiance_difference_wsum {};

        // Screen coverage of the cache, used for basis spawn
        // f16x2
        GfxTexture   cache_coverage_texture {};

        // Hierarchical z-buffer
        // R32_FLOAT, 1/2 - 1/8 resolution, 3 mip levels
        GfxTexture   HiZ_min {};
        GfxTexture   HiZ_max {};

        GfxTexture   depth {};

    } tex_ {};

    struct MIGIBuffers {
        GfxBuffer active_basis_count {};
        GfxBuffer active_basis_index {};
        GfxBuffer basis_effective_radius {};
        GfxBuffer basis_film_position {};
        GfxBuffer basis_effective_radius_film {};
        GfxBuffer basis_location {};
        GfxBuffer basis_parameter {};
        GfxBuffer quantilized_basis_step {};
        GfxBuffer basis_flags {};
        GfxBuffer free_basis_indices {};
        GfxBuffer free_basis_indices_count {};
        GfxBuffer tile_basis_count {};
        GfxBuffer tile_basis_index_injection {};
        GfxBuffer tile_base_slot_offset {};
        GfxBuffer tile_basis_index {};

        GfxBuffer dispatch_command {};
        GfxBuffer dispatch_rays_command {};
        GfxBuffer dispatch_count {};
        GfxBuffer draw_command {};
        GfxBuffer draw_indexed_command {};

        GfxBuffer disk_index_buffer {};
    } buf_{};

    bool initResources (const CapsaicinInternal &capsaicin);

    struct MIGIKernels {

        GfxProgram program {};

        GfxKernel  purge_tiles {};
        GfxKernel  clear_counters {};
        GfxKernel  trace_update_rays {};
        GfxKernel  clear_reservoirs {};
        GfxKernel  generate_reservoirs {};
        GfxKernel  compact_reservoirs {};
        GfxKernel  resample_reservoirs {};
        GfxKernel  populate_cells {};
        GfxKernel  generate_update_tiles_dispatch {};
        GfxKernel  update_tiles {};
        GfxKernel  resolve_cells {};

        GfxKernel  precompute_HiZ_min {};
        GfxKernel  precompute_HiZ_max {};

        GfxKernel  SSRC_clear_active_counter {};
        GfxKernel  SSRC_reproject_and_filter {};
        GfxKernel  SSRC_clear_tile_injection_index {};
        GfxKernel  SSRC_inject_generate_draw_indexed {};
        GfxKernel  SSRC_inject_reprojected_basis {};
        GfxKernel  SSRC_clip_overflow_tile_index {};
        GfxKernel  SSRC_allocate_extra_slot_for_basis_generation {};
        GfxKernel  SSRC_compress_tile_basis_index {};
        GfxKernel  SSRC_precompute_cache_update {};
        GfxKernel  SSRC_compute_cache_update_step {};
        GfxKernel  SSRC_apply_cache_update {};
        GfxKernel  SSRC_spawn_new_basis {};
        GfxKernel  SSRC_clip_over_allocation {};
        GfxKernel  SSRC_integrate_ASG {};

        GfxKernel  SSRC_reset {};

        GfxKernel  DebugSSRC_visualize_coverage {};
        GfxKernel  DebugSSRC_visualize_tile_occupancy {};
        GfxKernel  DebugSSRC_basis {};

        GfxKernel  generate_dispatch {};
        GfxKernel  generate_dispatch_rays {};

        GfxKernel  debug_hash_grid_cells {};


    } kernels_;

    bool initKernels (const CapsaicinInternal & capsaicin);

    void clearHashGridCache () ;

    void clearReservoirs () ;

protected:

    void updateRenderOptions (const CapsaicinInternal & capsaicin);

    void generateDispatch (GfxBuffer dispatch_count_buffer, uint threads_per_group);
    void generateDispatchRays (GfxBuffer count_buffer);

    MIGIRenderOptions options_ {};

    WorldSpaceReSTIR world_space_restir_;
    HashGridCache   hash_grid_cache_;

    GfxCamera previous_camera_ {};

    GfxSbt sbt_ {};

    // If the render dimensions have changed.
    bool need_resize_ {true};
    // If the hash grid cache debug view mode changed.
    bool need_reload_hash_grid_cache_debug_view_ {true};
    // If the kernel needs to be reloaded.
    // Note: the kernels is loaded upon initialization, so we do not need to set it to true.
    bool need_reload_kernel_ {false};
    // If the hash grid cache needs to be reset.
    bool need_reset_hash_grid_cache_ {true};
    // If the reservoirs need to be reset.
    bool need_reset_world_space_reservoirs_ {true};
    // If the screen space cache needs to be reset.
    bool need_reset_screen_space_cache_ {true};
    bool on_first_frame_ {true};
};
}

#endif // CAPSAICIN_MIGI_H
