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
            ImGui::LabelText("Active Basis", "%d", readback_values_.active_basis_count);
        }
        std::vector<std::string> debug_views = {"None", "SSRC_Coverage", "SSRC_TileOccupancy", "SSRC_Basis", "SSRC_Basis3D"};
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
                    options_.active_debug_view = e;
                    capsaicin.setDebugView(e);
                }
            }
            ImGui::EndCombo();
        }
        std::vector<std::string> debug_visualize_mode_names;
        if (options_.active_debug_view == "SSRC_Coverage")
        {
            debug_visualize_mode_names = {"Coverage"};
        }
        else if (options_.active_debug_view == "SSRC_TileOccupancy")
        {
            debug_visualize_mode_names = {"Heat", "Occupancy", "Occupancy PCol"};
        }
        else if (options_.active_debug_view == "SSRC_Basis")
        {
            debug_visualize_mode_names = {"Injection", "ID", "Radius", "PCol"};
        }
        else if(options_.active_debug_view == "SSRC_Basis3D") {
            debug_visualize_mode_names = {"Direction"};
        }
        if (debug_visualize_mode_names.empty())
        {
            debug_visualize_mode_names = {"None"};
        }
        options_.debug_visualize_mode = std::min(options_.debug_visualize_mode, static_cast<uint32_t>(debug_visualize_mode_names.size() - 1));
        if (ImGui::BeginCombo(
                "Debug Visualize Mode", debug_visualize_mode_names[options_.debug_visualize_mode].c_str()))
        {
            for (auto e : debug_visualize_mode_names)
            {
                if (ImGui::Selectable(e.c_str()))
                {
                    auto pos                      = std::distance(debug_visualize_mode_names.begin(),
                                             std::find(debug_visualize_mode_names.begin(), debug_visualize_mode_names.end(), e));
                    options_.debug_visualize_mode = static_cast<uint32_t>(pos);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Reset Screen Space Cache"))
        {
            need_reset_screen_space_cache_ = true;
        }
        ImGui::Checkbox("Always Reset", &options_.reset_screen_space_cache);
        ImGui::SliderFloat("Learing Rate", &options_.cache_update_learing_rate, 0.0f, 0.02f);
        ImGui::SliderFloat("W Initial Radius", &options_.SSRC_initial_W_radius, 3.0f, 32.0f);
        ImGui::SliderFloat("Min Coverage", &options_.SSRC_basis_spawn_coverage_threshold, 0.0f, 6.0f);
        ImGui::SliderFloat("Injection E", &options_.SSRC_min_weight_E, 0.0f, 0.2f);
        ImGui::Checkbox("Optimize SGColor", &options_.cache_update_SG_color);
        ImGui::Checkbox("Optimize SGDirection", &options_.cache_update_SG_direction);
        ImGui::Checkbox("Optimize SGLambda", &options_.cache_update_SG_lambda);
        ImGui::Checkbox("Optimize WLambda", &options_.cache_update_W_lambda);
        ImGui::Checkbox("Freeze Basis Allocation", &options_.freeze_basis_allocation);
    }
}
}