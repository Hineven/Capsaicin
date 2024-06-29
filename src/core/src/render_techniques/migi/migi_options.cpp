/*
 * Project Capsaicin: migi_options.cpp
 * Created: 2024/3/28
 * This program uses MulanPSL2. See LICENSE for more.
 */

#include "capsaicin_internal.h"
#include "components/blue_noise_sampler/blue_noise_sampler.h"
#include "components/brdf_lut/brdf_lut.h"
#include "components/light_sampler_grid_stream/light_sampler_grid_stream.h"
#include "components/stratified_sampler/stratified_sampler.h"
#include "migi.h"
#include "migi_internal.h"

namespace Capsaicin {

RenderOptionList MIGI::getRenderOptions() noexcept
{
    auto ret = RenderOptionList();
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
        need_reload_memory_ = true;
    }
    options_.width = new_width;
    options_.height = new_height;

    int uniform_probe_x = divideAndRoundUp(options_.width, SSRC_TILE_SIZE);
    int uniform_probe_y = divideAndRoundUp(options_.height, SSRC_TILE_SIZE);
    int uniform_probe_count = uniform_probe_x * uniform_probe_y;
    int max_probe_count = options_.SSRC_max_adaptive_probe_count + uniform_probe_count;
    if((int)options_.SSRC_max_probe_count != max_probe_count) {
        need_reload_memory_ = true;
    }
    options_.SSRC_max_probe_count      = max_probe_count;

    // Only SSRC update rays request ReSTIR sampling.
    options_.restir.max_query_ray_count = options_.SSRC_max_update_ray_count;


    auto new_enable_indirect = std::get<bool>(in["enable_indirect"]);
    if(options_.enable_indirect != new_enable_indirect) {
        need_reload_kernel_ = true;
    }
    options_.enable_indirect = new_enable_indirect;


    // Debugging

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
        if(io.MouseDown[1] && !io.WantCaptureMouse) {
            options_.cursor_dragging = true;
        } else {
            options_.cursor_dragging = false;
        }
    }

    // Reload flags
    // The screen space cache needs to be reset if the render state changes (i.e. camera transaction).
    need_reset_screen_space_cache_ |= options_.reset_screen_space_cache || capsaicin.getFrameIndex() == 0;
    need_reset_screen_space_cache_ |= need_reload_memory_ | need_reload_kernel_;
}

DebugViewList MIGI::getDebugViews() const noexcept
{
    auto ret = DebugViewList();
    ret.emplace_back("SSRC_ProbeAllocation");
    ret.emplace_back("SSRC_IncidentRadiance");
    ret.emplace_back("SSRC_UpdateRays");
    ret.emplace_back("SSRC_ReprojectionTrust");
    ret.emplace_back("SSRC_ProbeColor");
    return ret;
}

ComponentList MIGI::getComponents() const noexcept
{
    ComponentList components;
    components.emplace_back(COMPONENT_MAKE(LightSamplerGridStream));
    components.emplace_back(COMPONENT_MAKE(BlueNoiseSampler));
    components.emplace_back(COMPONENT_MAKE(StratifiedSampler));
    components.emplace_back(COMPONENT_MAKE(BrdfLut));
    return components;
}

}