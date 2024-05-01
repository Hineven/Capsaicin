/*
 * Project Capsaicin: migi_options.cpp
 * Created: 2024/3/28
 * This program uses MulanPSL2. See LICENSE for more.
 */

#include "capsaicin_internal.h"
#include "components/blue_noise_sampler/blue_noise_sampler.h"
#include "components/light_sampler_grid_stream/light_sampler_grid_stream.h"
#include "components/stratified_sampler/stratified_sampler.h"

#include "migi.h"

namespace Capsaicin {

RenderOptionList MIGI::getRenderOptions() noexcept
{
    auto ret = RenderOptionList();

//    ret.emplace("lr_rate", options_.cache_update_learing_rate);
//    ret.emplace("cache_update_SG_color", options_.cache_update_SG_color);
//    ret.emplace("cache_update_SG_direction", options_.cache_update_SG_direction);
//    ret.emplace("cache_update_SG_lambda", options_.cache_update_SG_lambda);
//    ret.emplace("cache_update_W_lambda", options_.cache_update_W_lambda);

//    ret.emplace("reset_screen_space_cache", options_.reset_screen_space_cache);

    ret.emplace("SSRC_max_basis_count", options_.SSRC_max_basis_count);
//    ret.emplace("SSRC_basis_spawn_coverage_threshold", options_.SSRC_basis_spawn_coverage_threshold);
//    ret.emplace("SSRC_min_weight_E", options_.SSRC_min_weight_E);
//    ret.emplace("SSRC_initial_W_radius", options_.SSRC_initial_W_radius);

    ret.emplace("shading_with_geometry_normal", options_.shading_with_geometry_normal);
    ret.emplace("no_importance_sampling", options_.no_importance_sampling);
    ret.emplace("fixed_step_size", options_.fixed_step_size);
    ret.emplace("enable_indirect", options_.enable_indirect);

    return ret;
}

AOVList MIGI::getAOVs() const noexcept
{
    auto aovs = AOVList{};
    aovs.push_back({"Debug", AOV::Write});
    aovs.push_back({"GlobalIllumination", AOV::Write, AOV::None, DXGI_FORMAT_R16G16B16A16_FLOAT});
    aovs.push_back({"Reflection", AOV::Write, AOV::None, DXGI_FORMAT_R16G16B16A16_FLOAT, "PrevReflection"});
    aovs.push_back({.name = "VisibilityDepth", .backup_name = "PrevVisibilityDepth"});
    aovs.push_back({.name = "GeometryNormal", .backup_name = "PrevGeometryNormal"});
    aovs.push_back({.name = "ShadingNormal", .backup_name = "PrevShadingNormal"});
    aovs.push_back({"Velocity"});
    aovs.push_back({.name = "Roughness", .backup_name = "PrevRoughness"});
    aovs.push_back({"OcclusionAndBentNormal"});
    aovs.push_back({"NearFieldGlobalIllumination"});
    aovs.push_back({"Visibility"});
    aovs.push_back({"PrevCombinedIllumination"});
    aovs.push_back({"DisocclusionMask"});
    return aovs;
}

void MIGI::updateRenderOptions(const CapsaicinInternal &capsaicin)
{
    auto in = capsaicin.getOptions();

    uint32_t new_width = capsaicin.getWidth();
    uint32_t new_height = capsaicin.getHeight();
    if(options_.width != new_width || options_.height != new_height) {
        need_resize_ = true;
    }
    options_.width = new_width;
    options_.height = new_height;

    assert(options_.SSRC_update_ray_budget * 1.5 <= options_.SSRC_max_update_ray_count);
    options_.SSRC_max_basis_count      = std::min((int)std::get<uint32_t>(in["SSRC_max_basis_count"]), cfg_.basis_buffer_allocation);

    // Only SSRC update rays request ReSTIR sampling.
    options_.restir.max_query_ray_count = options_.SSRC_max_update_ray_count;


    options_.shading_with_geometry_normal = std::get<bool>(in["shading_with_geometry_normal"]);
    options_.no_importance_sampling = std::get<bool>(in["no_importance_sampling"]);
    options_.fixed_step_size = std::get<bool>(in["fixed_step_size"]);
    auto new_enable_indirect = std::get<bool>(in["enable_indirect"]);
    if(options_.enable_indirect != new_enable_indirect) {
        need_reload_kernel_ = true;
    }
    options_.enable_indirect = new_enable_indirect;


    // Debugging
    options_.debug_freeze_frame_seed = (options_.active_debug_view == "SSRC_UpdateRays");
    options_.debug_view_switched = options_.active_debug_view != capsaicin.getCurrentDebugView();
    options_.active_debug_view = capsaicin.getCurrentDebugView();

    // Controls
    {
        auto & io = ImGui::GetIO();
        options_.cursor_pixel_coords = {std::max(io.MousePos.x, 0.f), std::max(io.MousePos.y, 0.f)};
        if(io.MouseClicked[1] && !io.WantCaptureMouse) {
            options_.cursor_clicked = true;
        } else {
            options_.cursor_clicked = false;
        }
    }

    // Reload flags
    need_reload_hash_grid_cache_debug_view_ = capsaicin.getCurrentDebugView() != options_.active_debug_view
                                            && ((options_.active_debug_view.starts_with("HashGridCache_")
                                                    && !capsaicin.getCurrentDebugView().starts_with("HashGridCache_"))
                                                || (!options_.active_debug_view.starts_with("HashGridCache_")
                                                    && capsaicin.getCurrentDebugView().starts_with("HashGridCache_")));

    // The screen space cache needs to be reset if the render state changes (i.e. camera transaction).
    need_reset_screen_space_cache_ |= options_.reset_screen_space_cache || capsaicin.getFrameIndex() == 0;
}

DebugViewList MIGI::getDebugViews() const noexcept
{
    auto ret = DebugViewList();
    ret.emplace_back("SSRC_Coverage");
    ret.emplace_back("SSRC_TileOccupancy");
    ret.emplace_back("SSRC_Basis");
    ret.emplace_back("SSRC_Basis3D");
    ret.emplace_back("SSRC_Difference");
    ret.emplace_back("SSRC_IncidentRadiance");
    ret.emplace_back("SSRC_UpdateRays");
    return ret;
}

ComponentList MIGI::getComponents() const noexcept
{
    ComponentList components;
    components.emplace_back(COMPONENT_MAKE(LightSamplerGridStream));
    components.emplace_back(COMPONENT_MAKE(BlueNoiseSampler));
    components.emplace_back(COMPONENT_MAKE(StratifiedSampler));
    return components;
}

}