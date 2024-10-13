/*
 * Project Capsaicin: migi_init.cpp
 * Created: 2024/3/28
 * This program uses MulanPSL2. See LICENSE for more.
 */
#include "../../math/math_constants.hlsl"
#include "capsaicin_internal.h"
#include "migi.h"

#include <functional>
#include <iostream>
#include <wtypesbase.h>

// Components for sampling and ray tracing
#include "components/blue_noise_sampler/blue_noise_sampler.h"
#include "components/light_sampler_grid_stream/light_sampler_grid_stream.h"
#include "components/stratified_sampler/stratified_sampler.h"
#include "migi_internal.h"

// Hack for missing functionalities in gfx

extern bool __override_primitive_topology;
extern D3D12_PRIMITIVE_TOPOLOGY_TYPE __override_primitive_topology_type;

namespace Capsaicin
{

bool MIGI::initConfig (const CapsaicinInternal & capsaicin) {


    (void) capsaicin; // Prevent unused variable warning
    // Check for wave operation support
    {
        auto                              dev      = gfxGetDevice(gfx_);
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 features = {};
        if (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &features, sizeof(features))))
        {
            std::cerr << "Failed to check for wave operation support" << std::endl;
            return false;
        }
        if (!features.WaveOps)
        {
            std::cerr << "MIGI requires wave operation support" << std::endl;
            return false;
        }
        if (features.WaveLaneCountMin != 32)
        {
            // TODO further support 64 lanes (RDNA)
            // see shaders for more details
            std::cerr << "MIGI is designed to operate on devices with 32 lanes" << std::endl;
            return false;
        }
        cfg_.wave_lane_count = features.WaveLaneCountMin;
        // Compute the streaming processor count
        cfg_.multiprocessing_core_count = features.TotalLaneCount / cfg_.wave_lane_count;
    }

    cfg_.basis_buffer_allocation = 1024 * 1024;

    return true;
}

bool MIGI::initKernels (const CapsaicinInternal & capsaicin) {
    // Create the program
    {
        kernels_.program = gfxCreateProgram(
            gfx_, "render_techniques/migi/migi", capsaicin.getShaderPath());
        if (!kernels_.program)
        {
            std::cerr << "Failed to create program for MIGI" << std::endl;
            return false;
        }
    }
    // Create kernels
    {
        auto defines = getShaderCompileDefinitions(capsaicin);
        std::vector<const char*> defines_c;
        for (auto &i : defines)
        {
            defines_c.push_back(i.c_str());
        }
        defines_c.push_back("HIZ_MIN");
        kernels_.PrecomputeHiZ_min = gfxCreateComputeKernel(
            gfx_, kernels_.program, "PrecomputeHiZ", defines_c.data(), (uint32_t)defines_c.size());
        defines_c.pop_back();
        kernels_.PrecomputeHiZ_max = gfxCreateComputeKernel(
            gfx_, kernels_.program, "PrecomputeHiZ", defines_c.data(), (uint32_t)defines_c.size());

        kernels_.WorldCache_Reset = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_Reset", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_ResetCounters = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_ResetCounters", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_WriteSpawnDispatchParameters = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_WriteSpawnDispatchParameters", defines_c.data(), (uint32_t)defines_c.size()
        );
        kernels_.WorldCache_SpawnProbes = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_SpawnProbes", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_RecycleProbes = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_RecycleProbes", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_ClearClipmaps = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_ClearClipmaps", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_UpdateActiveListAndIndex = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_UpdateActiveListAndIndex", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_ClipCounters = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_ClipCounters", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.GenerateDispatch = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateDispatch", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.GenerateDispatchRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateDispatchRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_ClearCounters = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ClearCounters", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_AllocateUniformProbes = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_AllocateUniformProbes", defines_c.data(), (uint32_t)defines_c.size());
        static std::string SSRC_MAX_ADAPTIVE_PROBE_LAYER_DEFINES[SSRC_MAX_ADAPTIVE_PROBE_LAYERS];
        for(int i = 0; i<SSRC_MAX_ADAPTIVE_PROBE_LAYERS; i++)
        {
            SSRC_MAX_ADAPTIVE_PROBE_LAYER_DEFINES[i] = "SSRC_ADAPTIVE_PROBE_LAYER=" + std::to_string(i);
            defines_c.push_back(SSRC_MAX_ADAPTIVE_PROBE_LAYER_DEFINES[i].c_str());
            kernels_.SSRC_AllocateAdaptiveProbes[i] = gfxCreateComputeKernel(gfx_, kernels_.program,
                "SSRC_AllocateAdaptiveProbes", defines_c.data(), (uint32_t)defines_c.size());
            defines_c.pop_back();
        }
        kernels_.WorldCache_WriteProbeDispatchParameters = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_WriteProbeDispatchParameters", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_WriteProbeDispatchParameters = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_WriteProbeDispatchParameters", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_ReprojectProbeHistory = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ReprojectProbeHistory", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_InitializeFailedProbes = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_InitializeFailedProbes", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_AllocateUpdateRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_AllocateUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_AllocateUpdateRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_AllocateUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.MIGI_SetUpdateRayCount = gfxCreateComputeKernel(
            gfx_, kernels_.program, "MIGI_SetUpdateRayCount", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_SampleUpdateRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_SampleUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_SampleUpdateRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_SampleUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.MIGI_GenerateTraceUpdateRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "MIGI_GenerateTraceUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        // MIGI_TraceUpdateRaysMain may be a DXR kernel, so it is created later
        kernels_.SSRC_ReprojectPreviousUpdateError = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ReprojectPreviousUpdateError", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_ShadeQueries = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_ShadeQueries", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_UpdateProbes = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_UpdateProbes", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.WorldCache_MoveProbes = gfxCreateComputeKernel(
            gfx_, kernels_.program, "WorldCache_MoveProbes", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_UpdateProbes = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_UpdateProbes", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_FilterProbes = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_FilterProbes", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_PadProbeTextureEdges = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_PadProbeTextureEdges", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_IntegrateASG = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_IntegrateASG", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_IntegrateDDGI = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_IntegrateDDGI", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_Denoise = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_Denoise", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.UEHemiOctahedronLutPrepare1 = gfxCreateComputeKernel(
            gfx_, kernels_.program, "UEHemiOctahedronLutPrepare1", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.UEHemiOctahedronLutPrepare2 = gfxCreateComputeKernel(
            gfx_, kernels_.program, "UEHemiOctahedronLutPrepare2", defines_c.data(), (uint32_t)defines_c.size());

        kernels_.Export = gfxCreateComputeKernel(
            gfx_, kernels_.program, "Export", defines_c.data(), (uint32_t)defines_c.size());

        kernels_.DebugSSRC_SetSelectedProbe = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_SetSelectedProbe", defines_c.data(), (uint32_t)defines_c.size());

        kernels_.DebugSSRC_FetchCursorPos = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_FetchCursorPos", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_VisualizeProbePlacement = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_VisualizeProbePlacement", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_PrepareProbeIncidentRadiance = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_PrepareProbeIncidentRadiance", defines_c.data(), (uint32_t)defines_c.size());
        // DebugSSRC_VisualizeIncidentRadiance is a graphics kernel and is created in initGraphicsKernels()
        kernels_.DebugSSRC_PrepareUpdateRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_PrepareUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_VisualizeReprojectionTrust = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_VisualizeReprojectionTrust", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_VisualizeProbeColor = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_VisualizeProbeColor", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugWorldCache_GenerateDraw = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugWorldCache_GenerateDraw", defines_c.data(), (uint32_t)defines_c.size());

        kernels_.DebugSSRC_EvalProbe = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_EvalProbe", defines_c.data(), (uint32_t)defines_c.size());

        if (options_.use_dxr10)
        {
            std::vector<char const *> base_subobjects;
            base_subobjects.push_back("MyShaderConfig");
            base_subobjects.push_back("MyPipelineConfig");

            std::vector<char const *> migi_cache_update_exports;
            migi_cache_update_exports.push_back(MIGIRT::kMIGICacheUpdateRaygenShaderName);
            migi_cache_update_exports.push_back(MIGIRT::kMIGICacheUpdateMissShaderName);
            migi_cache_update_exports.push_back(MIGIRT::kMIGICacheUpdateAnyHitShaderName);
            migi_cache_update_exports.push_back(MIGIRT::kMIGICacheUpdateClosestHitShaderName);
            std::vector<char const *> screen_cache_update_subobjects = base_subobjects;
            screen_cache_update_subobjects.push_back(MIGIRT::kMIGICacheUpdateHitGroupName);
            kernels_.MIGI_TraceUpdateRaysMain = gfxCreateRaytracingKernel(gfx_, kernels_.program, nullptr, 0,
                migi_cache_update_exports.data(), (uint32_t)migi_cache_update_exports.size(),
                screen_cache_update_subobjects.data(), (uint32_t)screen_cache_update_subobjects.size(),
                defines_c.data(), (uint32_t)defines_c.size());

            uint32_t entry_count[kGfxShaderGroupType_Count] {
                capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Raygen),
                capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Miss),
                gfxSceneGetInstanceCount(capsaicin.getScene())
                    * capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Hit),
                capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Callable)};
            GfxKernel sbt_kernels[] {kernels_.MIGI_TraceUpdateRaysMain};
            sbt_ = gfxCreateSbt(gfx_, sbt_kernels, ARRAYSIZE(sbt_kernels), entry_count);
        }
        else
        {
            kernels_.MIGI_TraceUpdateRaysMain                 = gfxCreateComputeKernel(
                gfx_, kernels_.program, "MIGI_TraceUpdateRaysMain", defines_c.data(), (uint32_t)defines_c.size());
        }

    }
    return true;
}

bool MIGI::initGraphicsKernels (const CapsaicinInternal & capsaicin) {

    auto defines = getShaderCompileDefinitions(capsaicin);
    std::vector<const char*> defines_c;
    for (auto &i : defines)
    {
        defines_c.push_back(i.c_str());
    }

    {
        GfxDrawState visualize_incident_radiance_draw_state {};
        gfxDrawStateSetColorTarget(
            visualize_incident_radiance_draw_state, 0, capsaicin.getAOVBuffer("Debug"));
        gfxDrawStateSetDepthStencilTarget(visualize_incident_radiance_draw_state, tex_.depth);
        kernels_.DebugSSRC_VisualizeIncidentRadiance =
            gfxCreateGraphicsKernel(gfx_, kernels_.program, visualize_incident_radiance_draw_state,
                "DebugSSRC_VisualizeIncidentRadiance", defines_c.data(), (uint32_t)defines_c.size());
    }

    {
        GfxDrawState visualize_probe_sg_direction_draw_state {};
        gfxDrawStateSetColorTarget(
            visualize_probe_sg_direction_draw_state, 0, capsaicin.getAOVBuffer("Debug"));
        gfxDrawStateSetDepthStencilTarget(visualize_probe_sg_direction_draw_state, tex_.depth);
        __override_primitive_topology      = true;
        __override_primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        kernels_.DebugSSRC_VisualizeProbeSGDirection =
            gfxCreateGraphicsKernel(gfx_, kernels_.program, visualize_probe_sg_direction_draw_state,
                "DebugSSRC_VisualizeProbeSGDirection", defines_c.data(), (uint32_t)defines_c.size());
        __override_primitive_topology      = false;
        __override_primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }

    {
        GfxDrawState visualize_update_rays_draw_state {};
        gfxDrawStateSetColorTarget(visualize_update_rays_draw_state, 0, capsaicin.getAOVBuffer("Debug"));
        gfxDrawStateSetDepthStencilTarget(visualize_update_rays_draw_state, tex_.depth);
        __override_primitive_topology      = true;
        __override_primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        kernels_.DebugSSRC_VisualizeUpdateRays =
            gfxCreateGraphicsKernel(gfx_, kernels_.program, visualize_update_rays_draw_state,
                "DebugSSRC_VisualizeUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        __override_primitive_topology      = false;
        __override_primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }

    {
        GfxDrawState visualize_light_draw_state {};
        gfxDrawStateSetColorTarget(visualize_light_draw_state, 0, capsaicin.getAOVBuffer("Debug"));
        gfxDrawStateSetDepthStencilTarget(visualize_light_draw_state, tex_.depth);
        kernels_.DebugSSRC_VisualizeLight =
            gfxCreateGraphicsKernel(gfx_, kernels_.program, visualize_light_draw_state,
                "DebugSSRC_VisualizeLight", defines_c.data(), (uint32_t)defines_c.size());
    }

    {
        GfxDrawState draw_state {};
        gfxDrawStateSetColorTarget(draw_state, 0, capsaicin.getAOVBuffer("Debug"));
        gfxDrawStateSetDepthStencilTarget(draw_state, tex_.depth);
        kernels_.DebugWorldCache_VisualizeProbes = gfxCreateGraphicsKernel(
            gfx_, kernels_.program, draw_state, "DebugWorldCache_VisualizeProbes", defines_c.data(),
            (uint32_t)defines_c.size());
    }

    {
        GfxDrawState draw_state {};
        gfxDrawStateSetColorTarget(draw_state, 0, capsaicin.getAOVBuffer("Debug"));
        gfxDrawStateSetDepthStencilTarget(draw_state, tex_.depth);
        kernels_.DebugSSRC_VisualizeProbe = gfxCreateGraphicsKernel(
            gfx_, kernels_.program, draw_state, "DebugSSRC_VisualizeProbe", defines_c.data(),
            (uint32_t)defines_c.size());
    }

    {
        __override_primitive_topology      = true;
        __override_primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        GfxDrawState draw_state {};
        gfxDrawStateSetColorTarget(draw_state, 0, capsaicin.getAOVBuffer("Debug"));
        gfxDrawStateSetDepthStencilTarget(draw_state, tex_.depth);
        kernels_.DebugSSRC_VisualizeProbeRays = gfxCreateGraphicsKernel(
            gfx_, kernels_.program, draw_state, "DebugSSRC_VisualizeProbeRays", defines_c.data(),
            (uint32_t)defines_c.size());
        __override_primitive_topology      = false;
        __override_primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
    return true;
}

bool MIGI::initResources (const CapsaicinInternal & capsaicin) {

    // Samplers
    clamped_point_sampler_ = gfxCreateSamplerState(gfx_, D3D12_FILTER_MIN_MAG_MIP_POINT);

    // Textures
    int probe_texture_width = divideAndRoundUp(capsaicin.getWidth(), SSRC_TILE_SIZE);
    int probe_texture_height_uniform = divideAndRoundUp(capsaicin.getHeight(), SSRC_TILE_SIZE);
    int probe_texture_height = probe_texture_height_uniform + divideAndRoundUp(options_.SSRC_max_adaptive_probe_count, probe_texture_width);
    if(probe_texture_height > 4096) {
        std::cerr << "(Probe texture) Overflowing texture dimensions: " << probe_texture_height << std::endl;
        return false;
    }

    tex_.probe_header_packed[0] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.probe_header_packed[0].setName("ProbeHeaderPacked0");
    tex_.probe_header_packed[1] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.probe_header_packed[1].setName("ProbeHeaderPacked1");

    tex_.probe_screen_coords[0] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.probe_screen_coords[0].setName("ProbeScreenCoords0");
    tex_.probe_screen_coords[1] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.probe_screen_coords[1].setName("ProbeScreenCoords1");

    tex_.probe_linear_depth[0] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32_FLOAT);
    tex_.probe_linear_depth[0].setName("ProbeLinearDepth0");
    tex_.probe_linear_depth[1] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32_FLOAT);
    tex_.probe_linear_depth[1].setName("ProbeLinearDepth1");

    tex_.probe_world_position[0] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    tex_.probe_world_position[0].setName("ProbeWorldPosition0");
    tex_.probe_world_position[1] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    tex_.probe_world_position[1].setName("ProbeWorldPosition1");

    tex_.probe_normal[0] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.probe_normal[0].setName("ProbeNormal0");
    tex_.probe_normal[1] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.probe_normal[1].setName("ProbeNormal1");

    tex_.probe_color[0] = gfxCreateTexture2D(gfx_, probe_texture_width * SSRC_PROBE_TEXTURE_SIZE, probe_texture_height * SSRC_PROBE_TEXTURE_SIZE, DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.probe_color[0].setName("ProbeColor0");
    tex_.probe_color[1] = gfxCreateTexture2D(gfx_, probe_texture_width * SSRC_PROBE_TEXTURE_SIZE, probe_texture_height * SSRC_PROBE_TEXTURE_SIZE, DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.probe_color[1].setName("ProbeColor1");
    tex_.probe_sample_color = gfxCreateTexture2D(gfx_, probe_texture_width * (SSRC_PROBE_TEXTURE_SIZE+2), probe_texture_height * (SSRC_PROBE_TEXTURE_SIZE+2), DXGI_FORMAT_R16G16B16A16_FLOAT);

    // Double the width to store all 8 coefficients
    tex_.probe_SH_coefficients_R = gfxCreateTexture2D(gfx_, probe_texture_width * 2, probe_texture_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.probe_SH_coefficients_R.setName("ProbeSHCoefficientsR");
    tex_.probe_SH_coefficients_G = gfxCreateTexture2D(gfx_, probe_texture_width * 2, probe_texture_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.probe_SH_coefficients_G.setName("ProbeSHCoefficientsG");
    tex_.probe_SH_coefficients_B = gfxCreateTexture2D(gfx_, probe_texture_width * 2, probe_texture_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.probe_SH_coefficients_B.setName("ProbeSHCoefficientsB");

    tex_.probe_irradiance = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.probe_irradiance.setName("ProbeIrradiance");

    tex_.probe_history_trust = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32_FLOAT);
    tex_.probe_history_trust.setName("ProbeHistoryTrust");

    tex_.probe_compensation = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.probe_compensation.setName("ProbeCompensation");

    int tile_texture_width = divideAndRoundUp(capsaicin.getWidth(), SSRC_TILE_SIZE);
    int tile_texture_height = divideAndRoundUp(capsaicin.getHeight(), SSRC_TILE_SIZE);

    // Note: R16 is not fully supported, it can cause strange behavior and silently fails on some hardware
    tex_.tile_adaptive_probe_count[0] = gfxCreateTexture2D(gfx_, tile_texture_width, tile_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.tile_adaptive_probe_count[0].setName("TileAdaptiveProbeCount0");
    tex_.tile_adaptive_probe_count[1] = gfxCreateTexture2D(gfx_, tile_texture_width, tile_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.tile_adaptive_probe_count[1].setName("TileAdaptiveProbeCount1");
    tex_.next_tile_adaptive_probe_count = gfxCreateTexture2D(gfx_, tile_texture_width, tile_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.next_tile_adaptive_probe_count.setName("NextTileAdaptiveProbeCount");

    int tile_index_texture_width = tile_texture_width * SSRC_TILE_SIZE;
    int tile_index_texture_height = tile_texture_height * SSRC_TILE_SIZE;
    tex_.tile_adaptive_probe_index[0] = gfxCreateTexture2D(gfx_, tile_index_texture_width, tile_index_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.tile_adaptive_probe_index[0].setName("TileAdaptiveProbeIndex0");
    tex_.tile_adaptive_probe_index[1] = gfxCreateTexture2D(gfx_, tile_index_texture_width, tile_index_texture_height, DXGI_FORMAT_R32_UINT);
    tex_.tile_adaptive_probe_index[1].setName("TileAdaptiveProbeIndex1");

    assert(capsaicin.getWidth() % 8 == 0 && capsaicin.getHeight() % 8 == 0);
    tex_.update_error_splat[0] = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16_FLOAT, SSRC_TILE_SIZE_L2 + 1);
    tex_.update_error_splat[0].setName("UpdateErrorSplat0");
    tex_.update_error_splat[1] = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16_FLOAT, SSRC_TILE_SIZE_L2 + 1);
    tex_.update_error_splat[1].setName("UpdateErrorSplat1");

    tex_.irradiance[0] = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.irradiance[0].setName("Irradiance0");
    tex_.irradiance[1] = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.irradiance[1].setName("Irradiance1");
    tex_.glossy_specular[0] = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.glossy_specular[0].setName("GlossySpecular0");
    tex_.glossy_specular[1] = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.glossy_specular[1].setName("GlossySpecular1");
    tex_.history_accumulation[0] = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16_UNORM);
    tex_.history_accumulation[0].setName("HistoryAccumulation0");
    tex_.history_accumulation[1] = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16_UNORM);
    tex_.history_accumulation[1].setName("HistoryAccumulation1");

    tex_.previous_global_illumination = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.previous_global_illumination.setName("PreviousGlobalIllumination");

    tex_.diffuse_GI[0] = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.diffuse_GI[0].setName("DiffuseGI0");
    tex_.diffuse_GI[1] = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.diffuse_GI[1].setName("DiffuseGI1");

    tex_.HiZ_min = gfxCreateTexture2D(gfx_, capsaicin.getWidth() / 2, capsaicin.getHeight() / 2, DXGI_FORMAT_R32_FLOAT, SSRC_TILE_SIZE_L2);
    tex_.HiZ_min.setName("HiZMin");
    tex_.HiZ_max = gfxCreateTexture2D(gfx_, capsaicin.getWidth() / 2, capsaicin.getHeight() / 2, DXGI_FORMAT_R32_FLOAT, SSRC_TILE_SIZE_L2);
    tex_.HiZ_max.setName("HiZMax");

    tex_.depth = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_D32_FLOAT);
    tex_.depth.setName("Depth (MIGI)");

    tex_.UE_hemi_octahedron_correction_lut = gfxCreateTexture2D(gfx_, SSRC_PROBE_TEXTURE_SIZE, SSRC_PROBE_TEXTURE_SIZE, DXGI_FORMAT_R32_FLOAT);
    tex_.UE_hemi_octahedron_correction_lut.setName("UEHemiOctahedronCorrectionLut");

    // Buffers
    buf_.count = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.count.setName("Count");
    buf_.dispatch_command = gfxCreateBuffer<DispatchCommand>(gfx_, 1);
    buf_.dispatch_command.setName("DispatchCommand");
    buf_.per_lane_dispatch_command = gfxCreateBuffer<DispatchCommand>(gfx_, 1);
    buf_.per_lane_dispatch_command.setName("DispatchCommand");
    buf_.dispatch_rays_command = gfxCreateBuffer<DispatchRaysCommand>(gfx_, 1);
    buf_.dispatch_rays_command.setName("DispatchRaysCommand");
    buf_.draw_command = gfxCreateBuffer<DrawCommand>(gfx_, 1);
    buf_.draw_command.setName("DrawCommand");
    buf_.draw_indexed_command = gfxCreateBuffer<DrawIndexedCommand>(gfx_, 1);
    buf_.draw_indexed_command.setName("DrawIndexedCommand");
    buf_.reduce_count = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.reduce_count.setName("ReduceCount");

    buf_.probe_SG[0] = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_basis_count);
    buf_.probe_SG[0].setName("ProbeSG0");
    buf_.probe_SG[1] = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_basis_count);
    buf_.probe_SG[1].setName("ProbeSG1");

    buf_.allocated_probe_SG_count = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.allocated_probe_SG_count.setName("AllocatedProbeSGCount");

    int max_probe_count = options_.SSRC_max_probe_count + options_.world_cache.max_probe_count;

    buf_.probe_update_ray_count = gfxCreateBuffer<uint32_t>(gfx_, max_probe_count);
    buf_.probe_update_ray_count.setName("ProbeUpdateRayCount");
    buf_.probe_update_ray_offset = gfxCreateBuffer<uint32_t>(gfx_, max_probe_count);
    buf_.probe_update_ray_offset.setName("ProbeUpdateRayOffset");

    buf_.update_ray_count = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.update_ray_count.setName("UpdateRayCount");

    int max_update_ray_count = options_.SSRC_max_update_ray_count + options_.world_cache.max_probe_count * options_.world_cache.num_update_ray_per_probe;

    buf_.update_ray_probe = gfxCreateBuffer<uint32_t>(gfx_, divideAndRoundUp(max_update_ray_count, cfg_.wave_lane_count));
    buf_.update_ray_probe.setName("UpdateRayProbe");
    buf_.update_ray_direction = gfxCreateBuffer<uint32_t>(gfx_, max_update_ray_count);
    buf_.update_ray_direction.setName("UpdateRayDirection");
    buf_.update_ray_radiance_inv_pdf = gfxCreateBuffer<uint32_t>(gfx_, max_update_ray_count * 2);
    buf_.update_ray_radiance_inv_pdf.setName("UpdateRayRadianceInvPdf");
    buf_.update_ray_linear_depth = gfxCreateBuffer<uint32_t>(gfx_, max_update_ray_count);
    buf_.update_ray_linear_depth.setName("UpdateRayLinearDepth");

    buf_.adaptive_probe_count = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.adaptive_probe_count.setName("AdaptiveProbeCount");
//    buf_.probe_update_error = gfxCreateBuffer<float>(gfx_, options_.SSRC_max_probe_count);
//    buf_.probe_update_error.setName("ProbeUpdateError");

    buf_.UE_hemi_octahedron_correction_lut_temp = gfxCreateBuffer<float>(gfx_, SSRC_PROBE_TEXTURE_TEXEL_COUNT * cfg_.multiprocessing_core_count);

    assert(options_.width % SSRC_TILE_SIZE == 0 && options_.height % SSRC_TILE_SIZE == 0);

    buf_.debug_probe_world_position = gfxCreateBuffer<float3>(gfx_, 1);
    buf_.debug_probe_world_position.setName("DebugProbeWorldPosition");
    buf_.debug_visualize_incident_radiance = gfxCreateBuffer<float3>(gfx_, cfg_.max_debug_visualize_incident_radiance_num_points);
    buf_.debug_visualize_incident_radiance.setName("DebugVisualizeIncidentRadiance");
    buf_.debug_visualize_incident_radiance_sum = gfxCreateBuffer<float>(gfx_, 1);
    buf_.debug_visualize_incident_radiance_sum.setName("DebugVisualizeIncidentRadianceSum");
    buf_.debug_cursor_world_pos = gfxCreateBuffer<float3>(gfx_, 1);
    buf_.debug_cursor_world_pos.setName("DebugCursorWorldPos");
    buf_.debug_probe_index = gfxCreateBuffer<uint32_t>(gfx_, 2);
    buf_.debug_probe_index.setName("DebugProbeIndex");

    for(auto & e : buf_.readback)
    {
        e = gfxCreateBuffer<uint32_t>(gfx_, 512, nullptr, GfxCpuAccess::kGfxCpuAccess_Read);
        e.setName((std::string("ReadbackBuffer[") + std::to_string(&e - buf_.readback) + "]").c_str());
    }

    buf_.export_binary   = gfxCreateBuffer<uint32_t>(gfx_, kExportBufferSize);
    buf_.export_staging = gfxCreateBuffer<uint32_t>(gfx_, kExportBufferSize, nullptr, GfxCpuAccess::kGfxCpuAccess_Read);

    // Generate ico sphere if it's not generated
    if(icosphere_vertices_.empty()) {
        std::function<void(glm::vec3,glm::vec3,glm::vec3,int)> gen = [&](glm::vec3 A, glm::vec3 B, glm::vec3 C, int remaining_depth) {
            glm::vec3 N = cross(B - A, C - A);
            glm::vec3 center = (A + B + C) / 3.f;
            if(dot(N, center) < 0) {
                std::swap(A, B);
            }
            if(remaining_depth == 0) {
                icosphere_vertices_.push_back(glm::normalize(A));
                icosphere_vertices_.push_back(glm::normalize(B));
                icosphere_vertices_.push_back(glm::normalize(C));
                glm::vec3 normal = glm::normalize(glm::cross(B - A, C - A));
                return;
            }
            glm::vec3 AB = glm::normalize((A + B) / 2.f);
            glm::vec3 BC = glm::normalize((B + C) / 2.f);
            glm::vec3 CA = glm::normalize((C + A) / 2.f);
            gen(A, AB, CA, remaining_depth - 1);
            gen(AB, B, BC, remaining_depth - 1);
            gen(CA, BC, C, remaining_depth - 1);
            gen(AB, BC, CA, remaining_depth - 1);
        };
        gen(glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), 8);
        gen(glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), glm::vec3(-1, 0, 0), 8);
        gen(glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, -1), 8);
        gen(glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), 8);
        gen(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, -1, 0), 8);
        gen(glm::vec3(0, 0, 1), glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0), 8);
        gen(glm::vec3(-1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), 8);
        gen(glm::vec3(0, 0, -1), glm::vec3(1, 0, 0), glm::vec3(0, -1, 0), 8);
    }

    buf_.icosphere_vertices = gfxCreateBuffer<float3>(gfx_, (uint32_t)icosphere_vertices_.size(), icosphere_vertices_.data());
    buf_.icosphere_vertices.setName("IcosphereVertices");
    buf_.vis_vpvn = gfxCreateBuffer<float4>(gfx_, (uint32_t)icosphere_vertices_.size() * 2);
    buf_.vis_vpvn.setName("VisVPVN");

    return true;
}


bool MIGI::init(const CapsaicinInternal &capsaicin) noexcept
{
    if(!initConfig(capsaicin))
    {
        return false;
    }
    updateRenderOptions(capsaicin);

    if(!initKernels(capsaicin)) {
        return false;
    }

    if(!initResources(capsaicin)) {
        return false;
    }

    if(!initGraphicsKernels(capsaicin)) {
        return false;
    }

    auto light_sampler = capsaicin.getComponent<LightSamplerGridStream>();
    light_sampler->reserveBoundsValues(capsaicin.getWidth() * capsaicin.getHeight(), this);
    memset(readback_pending_, 0, sizeof(readback_pending_));

    internal_frame_index_ = 0;

    need_reset_screen_space_cache_ = true;
    need_reset_world_space_reservoirs_ = true;
    need_reset_world_cache_ = true;

    return true;
}

void MIGI::releaseKernels()
{
    gfxDestroyKernel(gfx_, kernels_.PrecomputeHiZ_min);
    gfxDestroyKernel(gfx_, kernels_.PrecomputeHiZ_max);
    gfxDestroyKernel(gfx_, kernels_.GenerateDispatch);
    gfxDestroyKernel(gfx_, kernels_.GenerateDispatchRays);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_Reset);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_ResetCounters);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_WriteSpawnDispatchParameters);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_SpawnProbes);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_RecycleProbes);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_ClearClipmaps);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_UpdateActiveListAndIndex);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_ClipCounters);
    gfxDestroyKernel(gfx_, kernels_.SSRC_ClearCounters);
    gfxDestroyKernel(gfx_, kernels_.SSRC_AllocateUniformProbes);
    for(int i = 0; i<SSRC_MAX_ADAPTIVE_PROBE_LAYERS; i++)
    {
        gfxDestroyKernel(gfx_, kernels_.SSRC_AllocateAdaptiveProbes[i]);
    }
    gfxDestroyKernel(gfx_, kernels_.WorldCache_WriteProbeDispatchParameters);
    gfxDestroyKernel(gfx_, kernels_.SSRC_WriteProbeDispatchParameters);
    gfxDestroyKernel(gfx_, kernels_.SSRC_InitializeFailedProbes);
    gfxDestroyKernel(gfx_, kernels_.SSRC_ReprojectProbeHistory);
    gfxDestroyKernel(gfx_, kernels_.SSRC_AllocateUpdateRays);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_AllocateUpdateRays);
    gfxDestroyKernel(gfx_, kernels_.MIGI_SetUpdateRayCount);
    gfxDestroyKernel(gfx_, kernels_.SSRC_SampleUpdateRays);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_SampleUpdateRays);
    gfxDestroyKernel(gfx_, kernels_.MIGI_GenerateTraceUpdateRays);
    gfxDestroyKernel(gfx_, kernels_.MIGI_TraceUpdateRaysMain);
    gfxDestroyKernel(gfx_, kernels_.SSRC_ReprojectPreviousUpdateError);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_ShadeQueries);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_UpdateProbes);
    gfxDestroyKernel(gfx_, kernels_.WorldCache_MoveProbes);
    gfxDestroyKernel(gfx_, kernels_.SSRC_UpdateProbes);
    gfxDestroyKernel(gfx_, kernels_.SSRC_FilterProbes);
    gfxDestroyKernel(gfx_, kernels_.SSRC_PadProbeTextureEdges);
    gfxDestroyKernel(gfx_, kernels_.SSRC_IntegrateASG);
    gfxDestroyKernel(gfx_, kernels_.SSRC_IntegrateDDGI);
    gfxDestroyKernel(gfx_, kernels_.SSRC_Denoise);
    gfxDestroyKernel(gfx_, kernels_.UEHemiOctahedronLutPrepare1);
    gfxDestroyKernel(gfx_, kernels_.UEHemiOctahedronLutPrepare2);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_FetchCursorPos);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_VisualizeProbePlacement);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_PrepareProbeIncidentRadiance);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_VisualizeIncidentRadiance);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_VisualizeProbeSGDirection);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_PrepareUpdateRays);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_VisualizeReprojectionTrust);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_VisualizeProbeColor);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_VisualizeUpdateRays);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_VisualizeLight);
    gfxDestroyKernel(gfx_, kernels_.DebugWorldCache_GenerateDraw);
    gfxDestroyKernel(gfx_, kernels_.DebugWorldCache_VisualizeProbes);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_EvalProbe);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_VisualizeProbe);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_VisualizeProbeRays);
    gfxDestroyKernel(gfx_, kernels_.Export);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_SetSelectedProbe);

    gfxDestroyProgram(gfx_, kernels_.program);

    kernels_ = {};

    // Free SBT
    if (sbt_)
    {
        gfxDestroySbt(gfx_, sbt_);
        sbt_ = {};
    }
}

void MIGI::releaseResources()
{
    // Sampler
    gfxDestroySamplerState(gfx_, clamped_point_sampler_);

    // Free textures
    gfxDestroyTexture(gfx_, tex_.probe_header_packed[0]);
    gfxDestroyTexture(gfx_, tex_.probe_header_packed[1]);
    gfxDestroyTexture(gfx_, tex_.probe_screen_coords[0]);
    gfxDestroyTexture(gfx_, tex_.probe_screen_coords[1]);
    gfxDestroyTexture(gfx_, tex_.probe_linear_depth[0]);
    gfxDestroyTexture(gfx_, tex_.probe_linear_depth[1]);
    gfxDestroyTexture(gfx_, tex_.probe_world_position[0]);
    gfxDestroyTexture(gfx_, tex_.probe_world_position[1]);
    gfxDestroyTexture(gfx_, tex_.probe_normal[0]);
    gfxDestroyTexture(gfx_, tex_.probe_normal[1]);
    gfxDestroyTexture(gfx_, tex_.probe_color[0]);
    gfxDestroyTexture(gfx_, tex_.probe_color[1]);
    gfxDestroyTexture(gfx_, tex_.probe_sample_color);
    gfxDestroyTexture(gfx_, tex_.probe_SH_coefficients_R);
    gfxDestroyTexture(gfx_, tex_.probe_SH_coefficients_G);
    gfxDestroyTexture(gfx_, tex_.probe_SH_coefficients_B);
    gfxDestroyTexture(gfx_, tex_.probe_irradiance);
    gfxDestroyTexture(gfx_, tex_.probe_history_trust);
    gfxDestroyTexture(gfx_, tex_.probe_compensation);
    gfxDestroyTexture(gfx_, tex_.tile_adaptive_probe_count[0]);
    gfxDestroyTexture(gfx_, tex_.tile_adaptive_probe_count[1]);
    gfxDestroyTexture(gfx_, tex_.next_tile_adaptive_probe_count);
    gfxDestroyTexture(gfx_, tex_.tile_adaptive_probe_index[0]);
    gfxDestroyTexture(gfx_, tex_.tile_adaptive_probe_index[1]);
    gfxDestroyTexture(gfx_, tex_.update_error_splat[0]);
    gfxDestroyTexture(gfx_, tex_.update_error_splat[1]);
    gfxDestroyTexture(gfx_, tex_.history_accumulation[0]);
    gfxDestroyTexture(gfx_, tex_.history_accumulation[1]);
    gfxDestroyTexture(gfx_, tex_.previous_global_illumination);
    gfxDestroyTexture(gfx_, tex_.diffuse_GI[0]);
    gfxDestroyTexture(gfx_, tex_.diffuse_GI[1]);
    gfxDestroyTexture(gfx_, tex_.HiZ_min);
    gfxDestroyTexture(gfx_, tex_.HiZ_max);
    gfxDestroyTexture(gfx_, tex_.depth);
    gfxDestroyTexture(gfx_, tex_.UE_hemi_octahedron_correction_lut);

    // Free buffers
    gfxDestroyBuffer(gfx_, buf_.count);
    gfxDestroyBuffer(gfx_, buf_.dispatch_command);
    gfxDestroyBuffer(gfx_, buf_.per_lane_dispatch_command);
    gfxDestroyBuffer(gfx_, buf_.dispatch_rays_command);
    gfxDestroyBuffer(gfx_, buf_.draw_command);
    gfxDestroyBuffer(gfx_, buf_.draw_indexed_command);
    gfxDestroyBuffer(gfx_, buf_.reduce_count);
    gfxDestroyBuffer(gfx_, buf_.probe_SG[0]);
    gfxDestroyBuffer(gfx_, buf_.probe_SG[1]);
    gfxDestroyBuffer(gfx_, buf_.allocated_probe_SG_count);
    gfxDestroyBuffer(gfx_, buf_.probe_update_ray_count);
    gfxDestroyBuffer(gfx_, buf_.probe_update_ray_offset);
    gfxDestroyBuffer(gfx_, buf_.update_ray_count);
    gfxDestroyBuffer(gfx_, buf_.update_ray_probe);
    gfxDestroyBuffer(gfx_, buf_.update_ray_direction);
    gfxDestroyBuffer(gfx_, buf_.update_ray_radiance_inv_pdf);
    gfxDestroyBuffer(gfx_, buf_.update_ray_linear_depth);
    gfxDestroyBuffer(gfx_, buf_.adaptive_probe_count);
//    gfxDestroyBuffer(gfx_, buf_.probe_update_error);
    gfxDestroyBuffer(gfx_, buf_.UE_hemi_octahedron_correction_lut_temp);
    gfxDestroyBuffer(gfx_, buf_.icosphere_vertices);
    gfxDestroyBuffer(gfx_, buf_.vis_vpvn);
    gfxDestroyBuffer(gfx_, buf_.debug_cursor_world_pos);
    gfxDestroyBuffer(gfx_, buf_.debug_probe_world_position);
    gfxDestroyBuffer(gfx_, buf_.debug_visualize_incident_radiance);
    gfxDestroyBuffer(gfx_, buf_.debug_visualize_incident_radiance_sum);
    gfxDestroyBuffer(gfx_, buf_.debug_probe_index);

    gfxDestroyBuffer(gfx_, buf_.export_binary);
    gfxDestroyBuffer(gfx_, buf_.export_staging);
    for(const auto & i : buf_.readback)
    {
        gfxDestroyBuffer(gfx_, i);
    }

    tex_ = {};
    buf_ = {};
}

void MIGI::terminate() noexcept
{
    // Config
    cfg_ = {};
    releaseKernels();
    releaseResources();
}

std::vector<std::string> MIGI::getShaderCompileDefinitions(const CapsaicinInternal & capsaicin) const
{
    std::vector<std::string> ret;

    ret.push_back("WAVE_SIZE=" + std::to_string(cfg_.wave_lane_count));

    auto                     light_sampler = capsaicin.getComponent<LightSamplerGridStream>();
    std::vector<std::string> light_sampler_defines(std::move(light_sampler->getShaderDefines(capsaicin)));

    for(auto e : light_sampler_defines) ret.push_back(e);

    if (capsaicin.hasAOVBuffer("OcclusionAndBentNormal")) ret.emplace_back("HAS_OCCLUSION");
    ret.emplace_back("USE_RESAMPLING");

    if(options_.enable_indirect) ret.emplace_back("ENABLE_INDIRECT");

    if (capsaicin.getCurrentDebugView().starts_with("HashGridCache_"))
    {
        ret.emplace_back("DEBUG_HASH_CELLS");
    }

    return ret;
}

}