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
    ret.emplace("debug_visualize_mode", options_.debug_visualize_mode);
    ret.emplace("debug_visualize_channel", options_.debug_visualize_channel);

    ret.emplace("lr_rate", options_.cache_update_learing_rate);
    ret.emplace("cache_update_SG_color", options_.cache_update_SG_color);
    ret.emplace("cache_update_SG_direction", options_.cache_update_SG_direction);
    ret.emplace("cache_update_SG_lambda", options_.cache_update_SG_lambda);
    ret.emplace("cache_update_W_lambda", options_.cache_update_W_lambda);

    ret.emplace("reset_screen_space_cache", options_.reset_screen_space_cache);

    ret.emplace("SSRC_max_basis_count", options_.SSRC_max_basis_count);
    ret.emplace("SSRC_basis_spawn_coverage_threshold", options_.SSRC_basis_spawn_coverage_threshold);
    ret.emplace("SSRC_min_weight_E", options_.SSRC_min_weight_E);
    ret.emplace("SSRC_initial_W_radius", options_.SSRC_initial_W_radius);

    ret.emplace("shading_with_geometry_normal", options_.shading_with_geometry_normal);
    ret.emplace("no_importance_sampling", options_.no_importance_sampling);
    ret.emplace("fixed_step_size", options_.fixed_step_size);
    ret.emplace("use_blue_noise_sample_direction", options_.use_blue_noise_sample_direction);
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

    options_.debug_visualize_mode = std::get<uint32_t>(in["debug_visualize_mode"]);
    options_.debug_visualize_channel = std::get<uint32_t>(in["debug_visualize_channel"]);

    uint32_t new_width = capsaicin.getWidth();
    uint32_t new_height = capsaicin.getHeight();
    if(options_.width != new_width || options_.height != new_height) {
        need_resize_ = true;
    }
    options_.width = new_width;
    options_.height = new_height;

    // We shoot one update ray per pixel.
    options_.SSRC_max_update_ray_count = options_.width * options_.height;
    options_.SSRC_max_basis_count      = std::min((int)std::get<uint32_t>(in["SSRC_max_basis_count"]), cfg_.basis_buffer_allocation);
    options_.SSRC_basis_spawn_coverage_threshold = std::get<float>(in["SSRC_basis_spawn_coverage_threshold"]);
    options_.SSRC_min_weight_E         = std::get<float>(in["SSRC_min_weight_E"]);
    options_.SSRC_initial_W_radius     = std::get<float>(in["SSRC_initial_W_radius"]);

    // Only SSRC update rays request ReSTIR sampling.
    options_.restir.max_query_ray_count = options_.SSRC_max_update_ray_count;

    // GI Parameters
    // Read the options from the render settings and update options_.
    // This is called before rendering.
    options_.cache_update_learing_rate = std::get<float>(in["lr_rate"]);
    options_.cache_update_SG_color = std::get<bool>(in["cache_update_SG_color"]);
    options_.cache_update_SG_direction = std::get<bool>(in["cache_update_SG_direction"]);
    options_.cache_update_SG_lambda = std::get<bool>(in["cache_update_SG_lambda"]);
    options_.cache_update_W_lambda = std::get<bool>(in["cache_update_W_lambda"]);

    options_.shading_with_geometry_normal = std::get<bool>(in["shading_with_geometry_normal"]);
    options_.no_importance_sampling = std::get<bool>(in["no_importance_sampling"]);
    options_.fixed_step_size = std::get<bool>(in["fixed_step_size"]);
    options_.use_blue_noise_sample_direction = std::get<bool>(in["use_blue_noise_sample_direction"]);
    options_.reset_screen_space_cache = std::get<bool>(in["reset_screen_space_cache"]);
    auto new_enable_indirect = std::get<bool>(in["enable_indirect"]);
    if(options_.enable_indirect != new_enable_indirect) {
        need_reload_kernel_ = true;
    }
    options_.enable_indirect = new_enable_indirect;


    // Debugging
    options_.active_debug_view = capsaicin.getCurrentDebugView();

    // Reload flags
    need_reload_hash_grid_cache_debug_view_ = capsaicin.getCurrentDebugView() != options_.active_debug_view
                                            && ((options_.active_debug_view.starts_with("HashGridCache_")
                                                    && !capsaicin.getCurrentDebugView().starts_with("HashGridCache_"))
                                                || (!options_.active_debug_view.starts_with("HashGridCache_")
                                                    && capsaicin.getCurrentDebugView().starts_with("HashGridCache_")));

    need_reset_screen_space_cache_ = options_.reset_screen_space_cache;

}

DebugViewList MIGI::getDebugViews() const noexcept
{
    auto ret = DebugViewList();
    ret.emplace_back("SSRC_Coverage");
    ret.emplace_back("SSRC_TileOccupancy");
    ret.emplace_back("SSRC_Basis");
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