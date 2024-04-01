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
    ret.emplace("direction_x", options_.sg_direction.x);
    ret.emplace("direction_y", options_.sg_direction.y);
    ret.emplace("direction_z", options_.sg_direction.z);
    ret.emplace("light_x", options_.sg_li_position.x);
    ret.emplace("light_y", options_.sg_li_position.y);
    ret.emplace("light_z", options_.sg_li_position.z);
    ret.emplace("lambda", options_.sg_lambda);
    ret.emplace("intensity", options_.sg_intensity);
    ret.emplace("roughness", options_.roughness);
    ret.emplace("color_r", options_.sg_color.x);
    ret.emplace("color_g", options_.sg_color.y);
    ret.emplace("color_b", options_.sg_color.z);
    ret.emplace("lr_rate", options_.lr_rate);

    ret.emplace("channeled_cache", options_.channeled_cache);
    ret.emplace("shading_with_geometry_normal", options_.shading_with_geometry_normal);

    ret.emplace("reset_screen_space_cache", options_.reset_screen_space_cache);
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

void MIGI::updateRenderOptions(CapsaicinInternal &capsaicin)
{

    uint32_t new_width = capsaicin.getWidth();
    uint32_t new_height = capsaicin.getHeight();
    if(options_.width != new_width || options_.height != new_height) {
        need_resize_ = true;
    }
    options_.width = new_width;
    options_.height = new_height;

    // We shoot one update ray per pixel.
    options_.max_SSRC_update_ray_count  = options_.width * options_.height;

    // Only SSRC update rays request ReSTIR sampling.
    options_.restir.max_query_ray_count = options_.max_SSRC_update_ray_count;

    // GI Parameters
    // Read the options from the render settings and update options_.
    // This is called before rendering.
    options_.sg_direction.x = std::get<float>(capsaicin.getOptions()["direction_x"]);
    options_.sg_direction.y = std::get<float>(capsaicin.getOptions()["direction_y"]);
    options_.sg_direction.z = std::get<float>(capsaicin.getOptions()["direction_z"]);
    options_.sg_li_position.x = std::get<float>(capsaicin.getOptions()["light_x"]);
    options_.sg_li_position.y = std::get<float>(capsaicin.getOptions()["light_y"]);
    options_.sg_li_position.z = std::get<float>(capsaicin.getOptions()["light_z"]);
    options_.sg_lambda = std::get<float>(capsaicin.getOptions()["lambda"]);
    options_.sg_intensity = std::get<float>(capsaicin.getOptions()["intensity"]);
    options_.roughness = std::get<float>(capsaicin.getOptions()["roughness"]);
    options_.sg_color.x = std::get<float>(capsaicin.getOptions()["color_r"]);
    options_.sg_color.y = std::get<float>(capsaicin.getOptions()["color_g"]);
    options_.sg_color.z = std::get<float>(capsaicin.getOptions()["color_b"]);
    options_.lr_rate = std::get<float>(capsaicin.getOptions()["lr_rate"]);


    options_.channeled_cache = std::get<bool>(capsaicin.getOptions()["channeled_cache"]);
    options_.shading_with_geometry_normal = std::get<bool>(capsaicin.getOptions()["shading_with_geometry_normal"]);
    options_.reset_screen_space_cache = std::get<bool>(capsaicin.getOptions()["reset_screen_space_cache"]);


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
    ret.emplace_back("ScreenSpaceCache");
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