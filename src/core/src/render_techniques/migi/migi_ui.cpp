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
            static MIGIReadBackValues kept_readback_values = readback_values_;
            static bool always_reset = false;
            ImGui::Checkbox("Always Readback", &always_reset);
            if (ImGui::Button("Reread Statistics") || always_reset)
            {
                kept_readback_values = readback_values_;
            }
            ImGui::LabelText("Adaptive Probe", "%d", kept_readback_values.adaptive_probe_count);
            int probe_count = (int)kept_readback_values.adaptive_probe_count + (int)(options_.width * options_.height) / (SSRC_TILE_SIZE * SSRC_TILE_SIZE);
            ImGui::LabelText("SG Allocation", "%d (%.2f)", kept_readback_values.allocated_probe_SG_count, (float)kept_readback_values.allocated_probe_SG_count / probe_count);
            ImGui::LabelText("Allocated Rays", "%d", kept_readback_values.update_ray_count);
            ImGui::LabelText("Irradiance (Debug)", "%.4f", kept_readback_values.debug_visualize_incident_irradiance);
            ImGui::LabelText("Reprojection Sample Probe Weights", "(%02.2f, %02.2f, %02.2f, %02.2f)",
                kept_readback_values.reprojection_sample_probe_weights[0],
                kept_readback_values.reprojection_sample_probe_weights[1],
                kept_readback_values.reprojection_sample_probe_weights[2],
                kept_readback_values.reprojection_sample_probe_weights[3]
            );
            char buffer1[256], buffer2[256], buffer3[256], buffer4[256];
            sprintf(buffer1, "(%05.2f, %05.2f, %05.2f, %05.2f)",
                kept_readback_values.anyvalues[0],
                kept_readback_values.anyvalues[1],
                kept_readback_values.anyvalues[2],
                kept_readback_values.anyvalues[3]
            );
            sprintf(buffer2, "(%05.2f, %05.2f, %05.2f, %05.2f)",
                kept_readback_values.anyvalues[4],
                kept_readback_values.anyvalues[5],
                kept_readback_values.anyvalues[6],
                kept_readback_values.anyvalues[7]
            );
            sprintf(buffer3, "(%05.2f, %05.2f, %05.2f, %05.2f)",
                kept_readback_values.anyvalues[8],
                kept_readback_values.anyvalues[9],
                kept_readback_values.anyvalues[10],
                kept_readback_values.anyvalues[11]
            );
            sprintf(buffer4, "(%05.2f, %05.2f, %05.2f, %05.2f)",
                kept_readback_values.anyvalues[12],
                kept_readback_values.anyvalues[13],
                kept_readback_values.anyvalues[14],
                kept_readback_values.anyvalues[15]
            );

            ImGui::LabelText("Fpv(0123)", "%s", buffer1);
            ImGui::LabelText("Fpv(4567)", "%s", buffer2);
            ImGui::LabelText("Fpv(89ab)", "%s", buffer3);
            ImGui::LabelText("Fpv(cdef)", "%s", buffer4);
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
            debug_visualize_channel_names = {"SH+SG", "Oct+SG", "Oct Only"};
        } else if(options_.active_debug_view == "SSRC_UpdateRays") {
            debug_visualize_channel_names = {"Rays", "TracedRays", "TracedRayDepths"};
        } else if(options_.active_debug_view == "SSRC_ProbeColor") {
            debug_visualize_channel_names = {"Color", "Depth"};
        } else if(options_.active_debug_view == "WorldCache") {
            debug_visualize_channel_names = {"Irradiance2P", "Momentum"};
        } else if(options_.active_debug_view == "SSRC_ProbeInspection") {
            debug_visualize_channel_names = {"Inspection"};
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
        ImGui::Checkbox("DDGI Final Gather", &options_.DDGI_final_gather);
        ImGui::Checkbox("Exclude Oct Lighting", &options_.exclude_oct_lighting);
        ImGui::Checkbox("Exclude SG Lighting", &options_.exclude_SG_lighting);
        if (ImGui::Button("Reset Screen Space Cache"))
        {
            need_reset_screen_space_cache_ = true;
        }
        if(ImGui::Button("Reset World Cache"))
        {
            need_reset_world_cache_ = true;
        }
        ImGui::Checkbox("Always Reset", &options_.reset_screen_space_cache);
        ImGui::SliderInt("Update Ray Bonus", (int*)&options_.SSRC_base_update_ray_waves, 1, SSRC_MAX_NUM_UPDATE_RAY_PER_PROBE / cfg_.wave_lane_count);
        ImGui::SliderFloat("Min Learning Rate", &options_.cache_update_learing_rate, 0.0f, 0.05f);
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
        ImGui::SliderFloat("SG Merging Threshold", &options_.SSRC_SG_merging_threshold, 0.1f, 1.f);
        ImGui::SliderFloat("SG Similarity Alpha", &options_.SSRC_SG_similarity_alpha, 0.002f, 0.5f);
        ImGui::SliderFloat("SG Lambda Learning Bonus", &options_.SSRC_SG_lambda_learning_bonus, 0.1f, 50.f);
        ImGui::SliderFloat("SG Color  Learning Bonus", &options_.SSRC_SG_color_learning_bonus, 0.05f, 5.f);
        ImGui::SliderFloat("SG Direction Learing Rate", &options_.SSRC_SG_direction_learing_rate, 0.01f, 0.3f);
        ImGui::Checkbox("Always Export", &options_.always_export);
        if(!options_.always_export) {
            if (ImGui::Button("Export Probe Data"))
            {
                need_export_ = true;
            }
        } else need_export_ = true;
        if(options_.active_debug_view == "SSRC_ProbeInspection") {
            ImGui::Checkbox("Inspection: Probe", &options_.Inspection_VisualizeProbe);
            if(options_.Inspection_VisualizeProbe)
            {
                ImGui::Checkbox("Inspection: SH", &options_.Inspection_SH);
                ImGui::Checkbox("Inspection: SG", &options_.Inspection_SG);
                ImGui::Checkbox("Inspection: Oct", &options_.Inspection_Oct);
            }
            if(options_.Inspection_SH && options_.Inspection_Oct) {
                options_.Inspection_Oct = false;
            }
            ImGui::Checkbox("Inspection: Ray", &options_.Inspection_VisualizeRays);
        }

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