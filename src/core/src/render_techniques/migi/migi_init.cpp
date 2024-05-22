/*
 * Project Capsaicin: migi_init.cpp
 * Created: 2024/3/28
 * This program uses MulanPSL2. See LICENSE for more.
 */
#include "migi.h"

#include "../../math/math_constants.hlsl"
#include "capsaicin_internal.h"

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
            std::cerr << "MIGI is designed to operate on devices with 32 lanes" << std::endl;
            return false;
        }
        cfg_.wave_lane_count = features.WaveLaneCountMin;
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

        kernels_.GenerateDispatch = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateDispatch", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.GenerateDispatchRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateDispatchRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.PurgeTiles = gfxCreateComputeKernel(
            gfx_, kernels_.program, "PurgeTiles", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.ClearCounters = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ClearCounters", defines_c.data(), (uint32_t)defines_c.size());
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
        kernels_.SSRC_WriteProbeDispatchParameters = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_WriteProbeDispatchParameters", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_ReprojectProbeHistory = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ReprojectProbeHistory", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_AllocateUpdateRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_AllocateUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_SetUpdateRayCount = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_SetUpdateRayCount", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_SampleUpdateRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_SampleUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_GenerateTraceUpdateRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_GenerateTraceUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        // SSRC_TraceUpdateRaysMain may be a DXR kernel, so it is created later
        kernels_.SSRC_ReprojectPreviousUpdateError = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ReprojectPreviousUpdateError", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.ClearReservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ClearReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.GenerateReservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.CompactReservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "CompactReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.ResampleReservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ResampleReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        // PopulateCells may be a DXR kernel, so it is created later
        kernels_.GenerateUpdateTilesDispatch = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateUpdateTilesDispatch", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.UpdateTiles = gfxCreateComputeKernel(
            gfx_, kernels_.program, "UpdateTiles", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.ResolveCells = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ResolveCells", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_UpdateProbes = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_UpdateProbes", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_IntegrateASG = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_IntegrateASG", defines_c.data(), (uint32_t)defines_c.size());

        kernels_.DebugSSRC_FetchCursorPos = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_FetchCursorPos", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_VisualizeProbePlacement = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_VisualizeProbePlacement", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_PrepareUpdateRays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_PrepareUpdateRays", defines_c.data(), (uint32_t)defines_c.size());

        if (options_.use_dxr10)
        {
            std::vector<char const *> base_subobjects;
            base_subobjects.push_back("MyShaderConfig");
            base_subobjects.push_back("MyPipelineConfig");

            std::vector<char const *> screen_cache_update_exports;
            screen_cache_update_exports.push_back(MIGIRT::kScreenCacheUpdateRaygenShaderName);
            screen_cache_update_exports.push_back(MIGIRT::kScreenCacheUpdateMissShaderName);
            screen_cache_update_exports.push_back(MIGIRT::kScreenCacheUpdateAnyHitShaderName);
            screen_cache_update_exports.push_back(MIGIRT::kScreenCacheUpdateClosestHitShaderName);
            std::vector<char const *> screen_cache_update_subobjects = base_subobjects;
            screen_cache_update_subobjects.push_back(MIGIRT::kScreenCacheUpdateHitGroupName);
            kernels_.SSRC_TraceUpdateRaysMain = gfxCreateRaytracingKernel(gfx_, kernels_.program, nullptr, 0,
                screen_cache_update_exports.data(), (uint32_t)screen_cache_update_exports.size(),
                screen_cache_update_subobjects.data(), (uint32_t)screen_cache_update_subobjects.size(),
                defines_c.data(), (uint32_t)defines_c.size());

            std::vector<char const *> populate_cells_kernel_exports;
            populate_cells_kernel_exports.push_back(MIGIRT::kPopulateCellsRaygenShaderName);
            populate_cells_kernel_exports.push_back(MIGIRT::kPopulateCellsMissShaderName);
            populate_cells_kernel_exports.push_back(MIGIRT::kPopulateCellsAnyHitShaderName);
            populate_cells_kernel_exports.push_back(MIGIRT::kPopulateCellsClosestHitShaderName);
            std::vector<char const *> populate_cells_kernel_subobjects = base_subobjects;
            populate_cells_kernel_subobjects.push_back(MIGIRT::kPopulateCellsHitGroupName);
            kernels_.PopulateCellsMain = gfxCreateRaytracingKernel(gfx_, kernels_.program, nullptr, 0,
                populate_cells_kernel_exports.data(), (uint32_t)populate_cells_kernel_exports.size(),
                populate_cells_kernel_subobjects.data(), (uint32_t)populate_cells_kernel_subobjects.size(),
                defines_c.data(), (uint32_t)defines_c.size());

            uint32_t entry_count[kGfxShaderGroupType_Count] {
                capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Raygen),
                capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Miss),
                gfxSceneGetInstanceCount(capsaicin.getScene())
                    * capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Hit),
                capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Callable)};
            GfxKernel sbt_kernels[] {kernels_.SSRC_TraceUpdateRaysMain, kernels_.PopulateCellsMain};
            sbt_ = gfxCreateSbt(gfx_, sbt_kernels, ARRAYSIZE(sbt_kernels), entry_count);
        }
        else
        {
            kernels_.SSRC_TraceUpdateRaysMain                 = gfxCreateComputeKernel(
                gfx_, kernels_.program, "SSRC_TraceUpdateRaysMain", defines_c.data(), (uint32_t)defines_c.size());
            kernels_.PopulateCellsMain = gfxCreateComputeKernel(
                gfx_, kernels_.program, "PopulateCellsMain", defines_c.data(), (uint32_t)defines_c.size());
        }

    }
    return true;
}

bool MIGI::initGraphicsKernels (const CapsaicinInternal & capsaicin) {
    (void)capsaicin;
    // Do nothing
    return true;
}

bool MIGI::initResources (const CapsaicinInternal & capsaicin) {

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

    tex_.probe_normal[0] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R16G16_UNORM);
    tex_.probe_normal[0].setName("ProbeNormal0");
    tex_.probe_normal[1] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R16G16_UNORM);
    tex_.probe_normal[1].setName("ProbeNormal1");

    tex_.probe_irradiance[0] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.probe_irradiance[0].setName("ProbeIrradiance0");
    tex_.probe_irradiance[1] = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.probe_irradiance[1].setName("ProbeIrradiance1");

    tex_.probe_history_trust = gfxCreateTexture2D(gfx_, probe_texture_width, probe_texture_height, DXGI_FORMAT_R32_FLOAT);
    tex_.probe_history_trust.setName("ProbeHistoryTrust");

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

    tex_.HiZ_min = gfxCreateTexture2D(gfx_, capsaicin.getWidth() / 2, capsaicin.getHeight() / 2, DXGI_FORMAT_R32_FLOAT, SSRC_TILE_SIZE_L2);
    tex_.HiZ_min.setName("HiZMin");
    tex_.HiZ_max = gfxCreateTexture2D(gfx_, capsaicin.getWidth() / 2, capsaicin.getHeight() / 2, DXGI_FORMAT_R32_FLOAT, SSRC_TILE_SIZE_L2);
    tex_.HiZ_max.setName("HiZMax");

    tex_.depth = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_D32_FLOAT);
    tex_.depth.setName("Depth (MIGI)");

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

    buf_.probe_update_ray_count = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_probe_count);
    buf_.probe_update_ray_count.setName("ProbeUpdateRayCount");
    buf_.probe_update_ray_offset = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_probe_count);
    buf_.probe_update_ray_offset.setName("ProbeUpdateRayOffset");

    buf_.update_ray_count = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.update_ray_count.setName("UpdateRayCount");

    buf_.update_ray_probe = gfxCreateBuffer<uint32_t>(gfx_, divideAndRoundUp(options_.SSRC_max_update_ray_count, cfg_.wave_lane_count));
    buf_.update_ray_probe.setName("UpdateRayProbe");
    buf_.update_ray_direction = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_update_ray_count);
    buf_.update_ray_direction.setName("UpdateRayDirection");
    buf_.update_ray_radiance_inv_pdf = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_update_ray_count * 2);
    buf_.update_ray_radiance_inv_pdf.setName("UpdateRayRadianceInvPdf");
    buf_.update_ray_linear_depth = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_update_ray_count);
    buf_.update_ray_linear_depth.setName("UpdateRayLinearDepth");

    buf_.adaptive_probe_count = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.adaptive_probe_count.setName("AdaptiveProbeCount");
    buf_.probe_update_error = gfxCreateBuffer<float>(gfx_, options_.SSRC_max_probe_count);
    buf_.probe_update_error.setName("ProbeUpdateError");

    assert(options_.width % SSRC_TILE_SIZE == 0 && options_.height % SSRC_TILE_SIZE == 0);

    buf_.debug_visualize_incident_radiance = gfxCreateBuffer<float3>(gfx_, cfg_.max_debug_visualize_incident_radiance_num_points);
    buf_.debug_visualize_incident_radiance.setName("DebugVisualizeIncidentRadiance");
    buf_.debug_visualize_incident_radiance_sum = gfxCreateBuffer<float>(gfx_, 1);
    buf_.debug_visualize_incident_radiance_sum.setName("DebugVisualizeIncidentRadianceSum");
    buf_.debug_cursor_world_pos = gfxCreateBuffer<float3>(gfx_, 1);
    buf_.debug_cursor_world_pos.setName("DebugCursorWorldPos");

    for(auto & e : buf_.readback)
    {
        e = gfxCreateBuffer<uint32_t>(gfx_, 32, nullptr, GfxCpuAccess::kGfxCpuAccess_Read);
        e.setName((std::string("ReadbackBuffer[") + std::to_string(&e - buf_.readback) + "]").c_str());
    }
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
    need_reset_hash_grid_cache_ = true;

    return true;
}

void MIGI::releaseKernels()
{
    gfxDestroyKernel(gfx_, kernels_.PrecomputeHiZ_min);
    gfxDestroyKernel(gfx_, kernels_.PrecomputeHiZ_max);
    gfxDestroyKernel(gfx_, kernels_.GenerateDispatch);
    gfxDestroyKernel(gfx_, kernels_.GenerateDispatchRays);
    gfxDestroyKernel(gfx_, kernels_.PurgeTiles);
    gfxDestroyKernel(gfx_, kernels_.ClearCounters);
    gfxDestroyKernel(gfx_, kernels_.SSRC_ClearCounters);
    gfxDestroyKernel(gfx_, kernels_.SSRC_AllocateUniformProbes);
    for(int i = 0; i<SSRC_MAX_ADAPTIVE_PROBE_LAYERS; i++)
    {
        gfxDestroyKernel(gfx_, kernels_.SSRC_AllocateAdaptiveProbes[i]);
    }
    gfxDestroyKernel(gfx_, kernels_.SSRC_WriteProbeDispatchParameters);
    gfxDestroyKernel(gfx_, kernels_.SSRC_ReprojectProbeHistory);
    gfxDestroyKernel(gfx_, kernels_.SSRC_AllocateUpdateRays);
    gfxDestroyKernel(gfx_, kernels_.SSRC_SetUpdateRayCount);
    gfxDestroyKernel(gfx_, kernels_.SSRC_SampleUpdateRays);
    gfxDestroyKernel(gfx_, kernels_.SSRC_GenerateTraceUpdateRays);
    gfxDestroyKernel(gfx_, kernels_.SSRC_TraceUpdateRaysMain);
    gfxDestroyKernel(gfx_, kernels_.SSRC_ReprojectPreviousUpdateError);
    gfxDestroyKernel(gfx_, kernels_.ClearReservoirs);
    gfxDestroyKernel(gfx_, kernels_.GenerateReservoirs);
    gfxDestroyKernel(gfx_, kernels_.CompactReservoirs);
    gfxDestroyKernel(gfx_, kernels_.ResampleReservoirs);
    gfxDestroyKernel(gfx_, kernels_.PopulateCellsMain);
    gfxDestroyKernel(gfx_, kernels_.GenerateUpdateTilesDispatch);
    gfxDestroyKernel(gfx_, kernels_.UpdateTiles);
    gfxDestroyKernel(gfx_, kernels_.ResolveCells);
    gfxDestroyKernel(gfx_, kernels_.SSRC_UpdateProbes);
    gfxDestroyKernel(gfx_, kernels_.SSRC_IntegrateASG);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_FetchCursorPos);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_VisualizeProbePlacement);
    gfxDestroyKernel(gfx_, kernels_.DebugSSRC_PrepareUpdateRays);

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
    gfxDestroyTexture(gfx_, tex_.probe_irradiance[0]);
    gfxDestroyTexture(gfx_, tex_.probe_irradiance[1]);
    gfxDestroyTexture(gfx_, tex_.probe_history_trust);
    gfxDestroyTexture(gfx_, tex_.tile_adaptive_probe_count[0]);
    gfxDestroyTexture(gfx_, tex_.tile_adaptive_probe_count[1]);
    gfxDestroyTexture(gfx_, tex_.next_tile_adaptive_probe_count);
    gfxDestroyTexture(gfx_, tex_.tile_adaptive_probe_index[0]);
    gfxDestroyTexture(gfx_, tex_.tile_adaptive_probe_index[1]);
    gfxDestroyTexture(gfx_, tex_.update_error_splat[0]);
    gfxDestroyTexture(gfx_, tex_.update_error_splat[1]);
    gfxDestroyTexture(gfx_, tex_.HiZ_min);
    gfxDestroyTexture(gfx_, tex_.HiZ_max);
    gfxDestroyTexture(gfx_, tex_.depth);

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
    gfxDestroyBuffer(gfx_, buf_.probe_update_error);
    gfxDestroyBuffer(gfx_, buf_.debug_cursor_world_pos);
    gfxDestroyBuffer(gfx_, buf_.debug_visualize_incident_radiance);
    gfxDestroyBuffer(gfx_, buf_.debug_visualize_incident_radiance_sum);

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