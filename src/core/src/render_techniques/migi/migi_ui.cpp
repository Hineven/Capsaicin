/*
 * Project Capsaicin: migi_ui.cpp
 * Created: 2024/4/19
 * This program uses MulanPSL2. See LICENSE for more.
 */
#include "migi.h"
#include "imgui.h"
#include "capsaicin_internal.h"
namespace Capsaicin
{
void MIGI::renderGUI(CapsaicinInternal &capsaicin) const noexcept
{
    if(ImGui::CollapsingHeader("MIGI", ImGuiTreeNodeFlags_DefaultOpen))
    {
        (void)capsaicin;
        if (ImGui::CollapsingHeader("MIGI Statistics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::LabelText("Adaptive Probe", "%d", readback_values_.adaptive_probe_count);
            int probe_count = (int)readback_values_.adaptive_probe_count + (int)(options_.width * options_.height) / (SSRC_TILE_SIZE * SSRC_TILE_SIZE);
            ImGui::LabelText("SG Allocation", "%d (%.2f)", readback_values_.allocated_probe_SG_count, (float)readback_values_.allocated_probe_SG_count / probe_count);
            ImGui::LabelText("Allocated Rays", "%d", readback_values_.update_ray_count);
            ImGui::LabelText("Irradiance (Debug)", "%.4f", readback_values_.debug_visualize_incident_irradiance);
        }
        std::vector<std::string> debug_views = {"None"};
        auto migi_debug_views = getDebugViews();
        debug_views.insert(debug_views.end(), migi_debug_views.begin(), migi_debug_views.end());
        auto view_it = std::find(debug_views.begin(), debug_views.end(), options_.active_debug_view);
        if(view_it == debug_views.end())
        {
            view_it = debug_views.begin();
        }
        if(ImGui::BeginCombo("View", debug_views[view_it - debug_views.begin()].c_str()))
        {
            for (const auto& e : debug_views)
            {
                if (ImGui::Selectable(e.c_str()))
                {
                    capsaicin.setDebugView(e);
                }
            }
            ImGui::EndCombo();
        }
        std::vector<std::string> debug_visualize_channel_names;
        if (options_.active_debug_view == "SSRC_ProbeAllocation")
        {
            debug_visualize_channel_names = {"Allocation"};
        } else if(options_.active_debug_view == "SSRC_IncidentRadiance") {
            debug_visualize_channel_names = {"Probe", "Pixel"};
        } else if(options_.active_debug_view == "SSRC_UpdateRays") {
            debug_visualize_channel_names = {"Rays", "TracedRays", "TracedRayDepths"};
        } else if(options_.active_debug_view == "SSRC_ProbeColor") {
            debug_visualize_channel_names = {"Color", "Depth"};
        }
        if (debug_visualize_channel_names.empty())
        {
            debug_visualize_channel_names = {"None"};
        }
        options_.debug_visualize_channel = std::min(options_.debug_visualize_channel, static_cast<uint32_t>(debug_visualize_channel_names.size() - 1));
        if (ImGui::BeginCombo(
                "Debug Visualize Mode", debug_visualize_channel_names[options_.debug_visualize_channel].c_str()))
        {
            for (auto e : debug_visualize_channel_names)
            {
                if (ImGui::Selectable(e.c_str()))
                {
                    auto pos                      = std::distance(debug_visualize_channel_names.begin(),
                                             std::find(debug_visualize_channel_names.begin(), debug_visualize_channel_names.end(), e));
                    options_.debug_visualize_channel = static_cast<uint32_t>(pos);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Reset Screen Space Cache"))
        {
            need_reset_screen_space_cache_ = true;
        }
        ImGui::Checkbox("Always Reset", &options_.reset_screen_space_cache);
        ImGui::SliderInt("Update Ray Bonus", (int*)&options_.SSRC_base_update_ray_waves, 1, SSRC_MAX_NUM_UPDATE_RAY_PER_PROBE / cfg_.wave_lane_count);
        ImGui::SliderFloat("Learning Rate", &options_.cache_update_learing_rate, 0.0f, 0.05f);
        ImGui::Checkbox("Optimize SGColor", &options_.cache_update_SG_color);
        ImGui::Checkbox("Optimize SGDirection", &options_.cache_update_SG_direction);
        ImGui::Checkbox("Optimize SGLambda", &options_.cache_update_SG_lambda);
        ImGui::Checkbox("No Importance Sampling", &options_.no_importance_sampling);
        ImGui::Checkbox("No Adaptive Probes", &options_.no_adaptive_probes);
        ImGui::Checkbox("No Denoiser", &options_.no_denoiser);
        ImGui::Checkbox("No Probe Filtering", &options_.no_probe_filtering);
        ImGui::Checkbox("Freeze Seed", &options_.debug_freeze_frame_seed);
        ImGui::Checkbox("Freeze Tile Jitter", &options_.debug_freeze_tile_jitter);
        ImGui::Checkbox("Ambient Occlusion", &options_.ambient_occlusion);
        ImGui::Checkbox("SSGI", &options_.near_field_global_illumination);
        ImGui::Checkbox("Disable SG", &options_.disable_SG);
        ImGui::Checkbox("Squared radiance weight for SG direction", &options_.SSRC_squared_SG_directional_weight);

        if(ImGui::CollapsingHeader("Misc")) {
            ImGui::SliderInt("IR Visualize Points", (int*)&options_.debug_visualize_incident_radiance_num_points, 1, cfg_.max_debug_visualize_incident_radiance_num_points);
            ImGui::Checkbox("Debug Light", &options_.debug_light);
            ImGui::SliderFloat("Light Position (X)", &options_.debug_light_position.x, -1.0f, 1.0f);
            ImGui::SliderFloat("Light Position (Y)", &options_.debug_light_position.y, -1.0f, 1.0f);
            ImGui::SliderFloat("Light Position (Z)", &options_.debug_light_position.z, -1.0f, 1.0f);
            ImGui::SliderFloat("Light Size", &options_.debug_light_size, 0.0f, 0.5f);
            ImGui::SliderFloat3("Light Color", &options_.debug_light_color.x, 0.0f, 5.0f);
            ImGui::SliderInt("Fixed Tile Jitter", &options_.fixed_tile_jitter, 0, 7);
            ImGui::SliderInt("Fixed Frame Seed", &options_.fixed_frame_seed, 0, 64);
        }
    }
}
}