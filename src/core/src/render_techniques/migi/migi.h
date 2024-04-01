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

    void render(CapsaicinInternal &capsaicin) noexcept override;

    [[nodiscard]] AOVList getAOVs() const noexcept override;

    [[nodiscard]] DebugViewList getDebugViews() const noexcept override;

    [[nodiscard]] ComponentList getComponents() const noexcept override;

    [[nodiscard]] std::vector<std::string> getShaderCompileDefinitions (const CapsaicinInternal &) const ;

    struct Config {
        int wave_lane_count {};
    } cfg_;
    bool initConfig (const CapsaicinInternal &capsaicin);

    struct MIGIResources
    {
        // R16G16B16A16_FLOAT (Direction, Lambda)
        GfxTexture basis_parameter {};
        // R16G16B16A16_FLOAT (Color, ...)
        GfxTexture basis_color {};

        // For debugging only
        GfxTexture basis_parameter_gradient {};
        GfxTexture basis_color_gradient {};

        // Albedo * X = diffuse radiance
        // R16G16B16A16_FLOAT
        GfxTexture   radiance_X {};
        // Y = specular radiance
        // R16G16B16A16_FLOAT
        GfxTexture   radiance_Y {};

        // Ray buffers for cache update
        // R8G8B8A8_UNORM
        GfxTexture   update_ray_direction {};
        // R16G16B16A16_FLOAT
        GfxTexture   update_ray_radiance {};
        // RayRadiance - CachedRadiance
        // R16G16B16A16_FLOAT
        GfxTexture   update_ray_radiance_difference {};

        GfxTexture   depth {};

    } tex_ {};

    struct MIGIBuffers {
        GfxBuffer dispatch_command {};
        GfxBuffer dispatch_count {};
        GfxBuffer draw_command {};
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
        GfxKernel  precompute_cache_update {};
        GfxKernel  update_cache_parameters {};
        GfxKernel  precompute_channeled_cache_update {};
        GfxKernel  update_channeled_cache_params {};
        GfxKernel  integrate_ASG {};
        GfxKernel  integrate_ASG_with_channeled_cache {};

        GfxKernel  generate_dispatch {};
        GfxKernel  reset_screen_space_cache {};

        GfxKernel  debug_hash_grid_cells {};


    } kernels_;

    bool initKernels (const CapsaicinInternal & capsaicin);

    void clearHashGridCache () ;

    void clearReservoirs () ;

    void clearScreenSpaceCache () ;

protected:

    void updateRenderOptions (CapsaicinInternal & capsaicin);

    void generateDispatch (GfxBuffer dispatch_count_buffer, uint threads_per_group);

    MIGIRenderOptions options_ {};

    WorldSpaceReSTIR world_space_restir_;
    HashGridCache   hash_grid_cache_;

    GfxCamera previous_camera_ {};

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
