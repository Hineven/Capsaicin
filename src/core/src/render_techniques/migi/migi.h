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
    uint32_t adaptive_probe_count {};
    uint32_t allocated_probe_SG_count {};
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

        // Probe header, uint32 per probe
        GfxTexture   probe_header_packed [2];
        // Probe screen position, 2xuint16 packed in uint32
        GfxTexture probe_screen_coords[2];
        // Probe linear depth   1xfloat32
        GfxTexture   probe_linear_depth [2];
        // Probe world position 4xfloat32 because 3xfloat32 is not supported in most hardware
        GfxTexture   probe_world_position [2];
        // Probe world normal   2xunorm16
        GfxTexture   probe_normal [2];

        // Probe irradiance     4xfloat16
        GfxTexture   probe_irradiance [2];

        // Used to measure the trust of reprojected result from last frame [0, 1]
        GfxTexture   probe_history_trust;

        // Tile adaptive probe count  uint32 (Don't use R16, there are silent bugs)
        GfxTexture   tile_adaptive_probe_count [2];
        GfxTexture   next_tile_adaptive_probe_count;
        // Tile adaptive probe index  uint16
        GfxTexture   tile_adaptive_probe_index [2];

        // Update error used to guide update ray spawnning
        // fp16x2
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
        GfxBuffer per_lane_dispatch_command {};
        GfxBuffer dispatch_rays_command {};
        GfxBuffer draw_command {};
        GfxBuffer draw_indexed_command {};
        GfxBuffer reduce_count {};
        GfxBuffer probe_SG[2] {};
        GfxBuffer allocated_probe_SG_count {};
        GfxBuffer probe_update_ray_count {};
        GfxBuffer probe_update_ray_offset {};
        GfxBuffer update_ray_count {};
        GfxBuffer update_ray_probe {};
        GfxBuffer update_ray_direction {};
        GfxBuffer update_ray_radiance_inv_pdf {};
        GfxBuffer update_ray_linear_depth {};
        GfxBuffer adaptive_probe_count {};
        GfxBuffer probe_update_error {};

        GfxBuffer debug_cursor_world_pos {};
        GfxBuffer debug_probe_world_position {};
        GfxBuffer debug_visualize_incident_radiance {};
        GfxBuffer debug_visualize_incident_radiance_sum {};

        GfxBuffer readback[kGfxConstant_BackBufferCount] {};
    } buf_{};

    bool initResources (const CapsaicinInternal &capsaicin);
    void releaseResources () ;

    struct MIGIKernels {

        GfxProgram program {};

        GfxKernel  PrecomputeHiZ_min {};
        GfxKernel  PrecomputeHiZ_max {};

        GfxKernel  PurgeTiles {};
        GfxKernel  ClearCounters {};
        GfxKernel  SSRC_ClearCounters {};
        GfxKernel  SSRC_AllocateUniformProbes {};
        GfxKernel  SSRC_AllocateAdaptiveProbes[SSRC_MAX_ADAPTIVE_PROBE_LAYERS] {};
        GfxKernel  SSRC_WriteProbeDispatchParameters {};
        GfxKernel  SSRC_ReprojectProbeHistory {};
        GfxKernel  SSRC_AllocateUpdateRays {};
        GfxKernel  SSRC_SetUpdateRayCount {};
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
        GfxKernel  DebugSSRC_VisualizeProbePlacement {};
        GfxKernel  DebugSSRC_PrepareProbeIncidentRadiance {};
        GfxKernel  DebugSSRC_VisualizeIncidentRadiance {};
        GfxKernel  DebugSSRC_PrepareUpdateRays {};

        GfxKernel  GenerateDispatch {};
        GfxKernel  GenerateDispatchRays {};

    } kernels_;

    // Called before initResources
    bool initKernels (const CapsaicinInternal & capsaicin);
    // Called  after initResources
    bool initGraphicsKernels (const CapsaicinInternal & capsaicin);
    void releaseKernels () ;

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

    // If the kernel needs to be reloaded.
    bool need_reload_kernel_ {false};
    // If the render resources should be reallocated.
    bool need_reload_memory_ {false};
    // If the screen space cache needs to be reset.
    mutable bool need_reset_screen_space_cache_ {true};
    // If the hash grid cache needs to be reset.
    bool need_reset_hash_grid_cache_ {true};
    // If the reservoirs need to be reset.
    bool need_reset_world_space_reservoirs_ {true};

    bool readback_pending_ [kGfxConstant_BackBufferCount] {};
    MIGIReadBackValues readback_values_;

    uint32_t internal_frame_index_ {};

    MIGI_Constants previous_constants_ {};
};

}

#endif // CAPSAICIN_MIGI_H
