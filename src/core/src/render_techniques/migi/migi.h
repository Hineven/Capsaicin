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

struct MIGIReadBackValues {
    uint32_t active_basis_count {};
    float    sum_step_scale {};
    uint32_t update_ray_count {};
    float    debug_visualize_incident_irradiance {};
};

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

    void renderGUI(CapsaicinInternal &capsaicin) const noexcept override;

    struct Config {
        int wave_lane_count {};
        int basis_buffer_allocation {};
        int max_debug_visualize_incident_radiance_num_points {1024 * 1024};
    } cfg_;
    bool initConfig (const CapsaicinInternal &capsaicin);

    struct MIGIResources
    {

        // Screen coverage of the cache, used for basis spawn
        // fp16x2
        GfxTexture cache_coverage {};

        // Update error used to guide update ray spawnning
        // fp16x2 with mipmaps up to the tile size (1/8 res), reprojection is done across frames
        GfxTexture   update_error_splat [2]{};

        // Hierarchical z-buffer
        // R32_FLOAT, 1/2 - 1/8 resolution, 3 mip levels
        GfxTexture   HiZ_min {};
        GfxTexture   HiZ_max {};

        GfxTexture   depth {};

    } tex_ {};

    struct MIGIBuffers {
        GfxBuffer count {};
        GfxBuffer dispatch_command {};
        GfxBuffer dispatch_rays_command {};
        GfxBuffer draw_command {};
        GfxBuffer draw_indexed_command {};
        GfxBuffer reduce_count {};
        GfxBuffer probe_SG[2] {};
        GfxBuffer allocated_probe_SG_count {};
        GfxBuffer probe_update_ray_count {};
        GfxBuffer probe_update_ray_offset {};
        GfxBuffer update_ray_probe {};
        GfxBuffer update_ray_direction {};
        GfxBuffer update_ray_radiance_inv_pdf {};
        GfxBuffer update_ray_linear_depth {};
        GfxBuffer adaptive_probe_count {};
        GfxBuffer probe_update_error {};

        GfxBuffer debug_cursor_world_pos {};
        GfxBuffer debug_visualize_incident_radiance {};
        GfxBuffer debug_visualize_incident_radiance_sum {};

        GfxBuffer readback[kGfxConstant_BackBufferCount] {};
    } buf_{};

    bool initResources (const CapsaicinInternal &capsaicin);

    struct MIGIKernels {

        GfxProgram program {};

        GfxKernel  PrecomputeHiZ_min {};
        GfxKernel  PrecomputeHiZ_max {};

        GfxKernel  SSRC_ClearCounters {};
        GfxKernel  SSRC_AllocateUniformProbes {};
        GfxKernel  SSRC_AllocateAdaptiveProbes[SSRC_MAX_ADAPTIVE_PROBE_LAYERS] {};
        GfxKernel  SSRC_WriteProbeDispatchParameters {};
        GfxKernel  SSRC_ReprojectProbeHistory {};
        GfxKernel  SSRC_AllocateUpdateRays {};
        GfxKernel  SSRC_SampleUpdateRays {};
        GfxKernel  SSRC_GenerateTraceUpdateRays {};
        GfxKernel  SSRC_TraceUpdateRaysMain {};
        GfxKernel  SSRC_ReprojectPreviousUpdateError {};
        GfxKernel  ClearReservoirs {};
        GfxKernel  GenerateReservoirs {};
        GfxKernel  CompactReservoirs {};
        GfxKernel  ResampleReservoirs {};
        GfxKernel  PopulateCellsMain {};
        GfxKernel  GenerateUpdateTilesDispatch {};
        GfxKernel  UpdateTiles {};
        GfxKernel  ResolveCells {};
        GfxKernel  SSRC_UpdateProbes {};
        GfxKernel  SSRC_IntegrateASG {};
        GfxKernel  DebugSSRC_FetchCursorPos {};
        GfxKernel  DebugSSRC_PrepareUpdateRays {};

        GfxKernel  GenerateDispatch {};
        GfxKernel  GenerateDispatchRays {};

    } kernels_;

    bool initKernels (const CapsaicinInternal & capsaicin);

    void clearHashGridCache () ;

    void clearReservoirs () ;

protected:

    void updateRenderOptions (const CapsaicinInternal & capsaicin);

    void generateDispatch (GfxBuffer dispatch_count_buffer, uint threads_per_group);
    void generateDispatchRays (GfxBuffer count_buffer);

    // We need to modify it in the GUI rendering
    mutable MIGIRenderOptions options_ {};

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
    mutable bool need_reset_screen_space_cache_ {true};

    bool readback_pending_ [kGfxConstant_BackBufferCount] {};
    MIGIReadBackValues readback_values_;

    uint32_t internal_frame_index_ {};
};
}

#endif // CAPSAICIN_MIGI_H
