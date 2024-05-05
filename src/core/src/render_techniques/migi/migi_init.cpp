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
        kernels_.precompute_HiZ_min = gfxCreateComputeKernel(
            gfx_, kernels_.program, "PrecomputeHiZ", defines_c.data(), (uint32_t)defines_c.size());
        defines_c.pop_back();
        kernels_.precompute_HiZ_max = gfxCreateComputeKernel(
            gfx_, kernels_.program, "PrecomputeHiZ", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_clear_active_counter = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ClearActiveCounter", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_reproject_and_filter = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ReprojectAndFilter", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_clear_tile_injection_index = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ClearTileInjectionIndex", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_inject_generate_draw_indexed = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_InjectGenerateDrawIndexed", defines_c.data(), (uint32_t)defines_c.size());
        GfxDrawState injection_draw_state = {};
        // No culling
        gfxDrawStateSetCullMode(injection_draw_state, D3D12_CULL_MODE_NONE);
        kernels_.SSRC_inject_reprojected_basis = gfxCreateGraphicsKernel(gfx_, kernels_.program, injection_draw_state,
            "SSRC_InjectReprojectedBasis", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_clip_overflow_tile_index = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ClipOverflowTileIndex", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_allocate_extra_slot_for_basis_generation = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_AllocateExtraSlotForBasisGeneration", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_compress_tile_basis_index = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_CompressTileBasisIndex", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_reproject_previous_update_error = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ReprojectPreviousUpdateError", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_precompute_ray_budget_for_tiles = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_PrecomputeRayBudgetForTiles", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_tiles_set_reduce_count_32 = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_TilesSetReduceCount32", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_tiles_set_reduce_count = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_TilesSetReduceCount", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_allocate_update_rays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_AllocateUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_sample_update_rays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_SampleUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_generate_trace_update_rays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_GenerateTraceUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        // SSRC_trace_update_rays may be a DXR kernel, so it is created later
        kernels_.purge_tiles = gfxCreateComputeKernel(
            gfx_, kernels_.program, "PurgeTiles", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.clear_counters = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ClearCounters", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.clear_reservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ClearReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.generate_reservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.compact_reservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "CompactReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.resample_reservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ResampleReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        // PopulateCells may be a DXR kernel, so it is created later
        kernels_.generate_update_tiles_dispatch = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateUpdateTilesDispatch", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.update_tiles = gfxCreateComputeKernel(
            gfx_, kernels_.program, "UpdateTiles", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.resolve_cells = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ResolveCells", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_precompute_cache_update = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_PrecomputeCacheUpdate", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_compute_cache_update_step = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ComputeCacheUpdateStep", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_normalize_cache_update = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_NormalizeCacheUpdate", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_normalize_cache_update_set_reduce_count = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_NormalizeCacheUpdateSetReduceCount", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_apply_cache_update = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ApplyCacheUpdate", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_spawn_new_basis = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_SpawnNewBasis", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_clip_over_allocation = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_ClipOverAllocation", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_integrate_ASG = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_IntegrateASG", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.SSRC_accumulate_update_error = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_AccumulateUpdateError", defines_c.data(), (uint32_t)defines_c.size());

        kernels_.SSRC_reset = gfxCreateComputeKernel(
            gfx_, kernels_.program, "SSRC_Reset", defines_c.data(), (uint32_t)defines_c.size());

        kernels_.DebugSSRC_visualize_coverage = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_VisualizeCoverage", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_visualize_tile_occupancy = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_VisualizeTileOccupancy", defines_c.data(), (uint32_t)defines_c.size());
        GfxDrawState debug_basis_draw_state = {};
        // No culling
        gfxDrawStateSetCullMode(debug_basis_draw_state, D3D12_CULL_MODE_NONE);
        gfxDrawStateSetDepthStencilTarget(debug_basis_draw_state, tex_.depth);
        gfxDrawStateSetColorTarget(debug_basis_draw_state, 0, capsaicin.getAOVBuffer("Debug"));
        kernels_.DebugSSRC_basis = gfxCreateGraphicsKernel(gfx_, kernels_.program, debug_basis_draw_state,
            "DebugSSRC_Basis", defines_c.data(), (uint32_t)defines_c.size());
        gfxDrawStateSetFillMode(debug_basis_draw_state, D3D12_FILL_MODE_WIREFRAME);
        kernels_.DebugSSRC_basis_3D = gfxCreateGraphicsKernel(gfx_, kernels_.program, debug_basis_draw_state,
            "DebugSSRC_Basis3D", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_generate_draw_indexed = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_GenerateDrawIndexed", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_show_difference = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_ShowDifference", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_fetch_cursor_pos = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_FetchCursorPos", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.DebugSSRC_precompute_incident_radiance = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_PrecomputeIncidentRadiance", defines_c.data(), (uint32_t)defines_c.size());
        GfxDrawState debug_incident_radiance_draw_state = {};
        // No culling
        gfxDrawStateSetCullMode(debug_incident_radiance_draw_state, D3D12_CULL_MODE_NONE);
        // Depth test on
        gfxDrawStateSetDepthStencilTarget(debug_incident_radiance_draw_state, tex_.depth);
        gfxDrawStateSetDepthWriteMask(debug_incident_radiance_draw_state, D3D12_DEPTH_WRITE_MASK_ALL);
        gfxDrawStateSetColorTarget(debug_incident_radiance_draw_state, 0, capsaicin.getAOVBuffer("Debug"));
        gfxDrawStateSetFillMode(debug_incident_radiance_draw_state, D3D12_FILL_MODE_SOLID);
        __override_primitive_topology = true;
        __override_primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        kernels_.DebugSSRC_incident_radiance = gfxCreateGraphicsKernel(gfx_, kernels_.program, debug_incident_radiance_draw_state,
            "DebugSSRC_IncidentRadiance", defines_c.data(), (uint32_t)defines_c.size());
        __override_primitive_topology = false;
        __override_primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        kernels_.DebugSSRC_prepare_update_rays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "DebugSSRC_PrepareUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        GfxDrawState debug_update_rays_draw_state = {};
        // No culling
        gfxDrawStateSetCullMode(debug_update_rays_draw_state, D3D12_CULL_MODE_NONE);
        // Depth test on but no write
        gfxDrawStateSetDepthStencilTarget(debug_update_rays_draw_state, tex_.depth);
        gfxDrawStateSetDepthWriteMask(debug_update_rays_draw_state, D3D12_DEPTH_WRITE_MASK_ZERO);
        gfxDrawStateSetColorTarget(debug_update_rays_draw_state, 0, capsaicin.getAOVBuffer("Debug"));
        gfxDrawStateSetFillMode(debug_update_rays_draw_state, D3D12_FILL_MODE_SOLID);
        __override_primitive_topology = true;
        __override_primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        kernels_.DebugSSRC_update_rays         = gfxCreateGraphicsKernel(gfx_, kernels_.program, debug_incident_radiance_draw_state,
            "DebugSSRC_UpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        __override_primitive_topology = false;
        __override_primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        kernels_.generate_dispatch = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateDispatch", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.generate_dispatch_rays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateDispatchRays", defines_c.data(), (uint32_t)defines_c.size());

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
            kernels_.SSRC_trace_update_rays = gfxCreateRaytracingKernel(gfx_, kernels_.program, nullptr, 0,
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
            kernels_.populate_cells = gfxCreateRaytracingKernel(gfx_, kernels_.program, nullptr, 0,
                populate_cells_kernel_exports.data(), (uint32_t)populate_cells_kernel_exports.size(),
                populate_cells_kernel_subobjects.data(), (uint32_t)populate_cells_kernel_subobjects.size(),
                defines_c.data(), (uint32_t)defines_c.size());

            //generate_dispatch_rays_kernel_ = gfxCreateComputeKernel(gfx_, kernels_.program, "GenerateDispatchRays");

            uint32_t entry_count[kGfxShaderGroupType_Count] {
                capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Raygen),
                capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Miss),
                gfxSceneGetInstanceCount(capsaicin.getScene())
                    * capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Hit),
                capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Callable)};
            GfxKernel sbt_kernels[] {kernels_.SSRC_trace_update_rays, kernels_.populate_cells};
            sbt_ = gfxCreateSbt(gfx_, sbt_kernels, ARRAYSIZE(sbt_kernels), entry_count);
        }
        else
        {
            kernels_.SSRC_trace_update_rays                 = gfxCreateComputeKernel(
                gfx_, kernels_.program, "SSRC_TraceUpdateRaysMain", defines_c.data(), (uint32_t)defines_c.size());
            kernels_.populate_cells = gfxCreateComputeKernel(
                gfx_, kernels_.program, "PopulateCellsMain", defines_c.data(), (uint32_t)defines_c.size());
        }

    }
    return true;
}

bool MIGI::initResources (const CapsaicinInternal & capsaicin) {
    auto divideAndRoundUp = [](uint32_t a, uint32_t b) -> uint32_t {
        return (a + b - 1) / b;
    };

    tex_.cache_coverage = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16_FLOAT, 1);
    tex_.cache_coverage.setName("CacheCoverageTexture");
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
    buf_.active_basis_count    = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.active_basis_count.setName("ActiveBasisCount");
    buf_.active_basis_index    = gfxCreateBuffer<uint32_t>(gfx_, cfg_.basis_buffer_allocation);
    buf_.active_basis_index.setName("ActiveBasisIndex");
    buf_.basis_effective_radius= gfxCreateBuffer<float>(gfx_, cfg_.basis_buffer_allocation);
    buf_.basis_effective_radius.setName("BasisEffectiveRadius");
    buf_.basis_film_position   = gfxCreateBuffer<uint32_t>(gfx_, cfg_.basis_buffer_allocation);
    buf_.basis_film_position.setName("BasisFilmPosition");
    buf_.basis_effective_radius_film = gfxCreateBuffer<float>(gfx_, cfg_.basis_buffer_allocation);
    buf_.basis_effective_radius_film.setName("BasisEffectiveRadiusFilm");
    buf_.basis_location        = gfxCreateBuffer<float3>(gfx_, cfg_.basis_buffer_allocation);
    buf_.basis_location.setName("BasisLocation");
    buf_.basis_parameter       = gfxCreateBuffer<float>(gfx_, cfg_.basis_buffer_allocation * 4);
    buf_.basis_parameter.setName("BasisParameter");
    buf_.quantilized_basis_step= gfxCreateBuffer<uint>(gfx_, cfg_.basis_buffer_allocation * 7);
    buf_.quantilized_basis_step.setName("QuantilizedBasisStep");
    buf_.basis_average_gradient_scale = gfxCreateBuffer<uint32_t>(gfx_, cfg_.basis_buffer_allocation * 2);
    buf_.basis_average_gradient_scale.setName("BasisAverageGradientScale");
    buf_.basis_flags           = gfxCreateBuffer<uint32_t>(gfx_, cfg_.basis_buffer_allocation);
    buf_.basis_flags.setName("BasisFlags");
    buf_.free_basis_indices    = gfxCreateBuffer<uint32_t>(gfx_, cfg_.basis_buffer_allocation);
    buf_.free_basis_indices.setName("FreeBasisIndices");
    buf_.free_basis_indices_count = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.free_basis_indices_count.setName("FreeBasisIndicesCount");
    assert(options_.width % SSRC_TILE_SIZE == 0 && options_.height % SSRC_TILE_SIZE == 0);
    int ssrc_tile_count = options_.width / SSRC_TILE_SIZE * options_.height / SSRC_TILE_SIZE;
    buf_.tile_basis_count      = gfxCreateBuffer<uint32_t>(gfx_, ssrc_tile_count);
    buf_.tile_basis_count.setName("TileBasisCount");
    buf_.tile_ray_count        = gfxCreateBuffer<uint32_t>(gfx_, ssrc_tile_count);
    buf_.tile_ray_count.setName("TileRayCount");
    buf_.tile_ray_offset       = gfxCreateBuffer<uint32_t>(gfx_, ssrc_tile_count);
    buf_.tile_ray_offset.setName("TileRayOffset");
    buf_.update_ray_direction  = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_update_ray_count);
    buf_.update_ray_direction.setName("UpdateRayDirection");
    buf_.update_ray_origin     = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_update_ray_count);
    buf_.update_ray_origin.setName("UpdateRayOrigin");
    buf_.update_ray_radiance_pdf = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_update_ray_count * 2);
    buf_.update_ray_radiance_pdf.setName("UpdateRayRadiancePdf");
    buf_.update_ray_cache      = gfxCreateBuffer<uint32_t>(gfx_, options_.SSRC_max_update_ray_count * 2);
    buf_.update_ray_cache.setName("UpdateRayCache");
    buf_.update_ray_count      = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.update_ray_count.setName("UpdateRayCount");
    buf_.tile_update_error_sums= gfxCreateBuffer<float>(gfx_, divideAndRoundUp(ssrc_tile_count, cfg_.wave_lane_count));
    buf_.tile_update_error_sums.setName("TileUpdateErrorSums");
    buf_.tile_update_error     = gfxCreateBuffer<float>(gfx_, 1);
    buf_.tile_update_error.setName("TileUpdateError");
    buf_.tile_basis_index_injection = gfxCreateBuffer<uint32_t>(gfx_, ssrc_tile_count * SSRC_MAX_BASIS_PER_TILE);
    buf_.tile_basis_index_injection.setName("TileBasisIndexInjection");
    buf_.tile_base_slot_offset = gfxCreateBuffer<uint32_t>(gfx_, ssrc_tile_count);
    buf_.tile_base_slot_offset.setName("TileBaseSlotOffset");
    buf_.tile_basis_index      = gfxCreateBuffer<uint32_t>(gfx_, ssrc_tile_count * (SSRC_MAX_BASIS_PER_TILE + 1));
    buf_.tile_basis_index.setName("TileBasisIndex");

    buf_.dispatch_count        = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.dispatch_count.setName("DispatchCount");
    buf_.dispatch_command      = gfxCreateBuffer<DispatchCommand>(gfx_, 1);
    buf_.dispatch_command.setName("DispatchCommand");
    buf_.dispatch_rays_command = gfxCreateBuffer<DispatchRaysCommand>(gfx_, 1);
    buf_.dispatch_rays_command.setName("DispatchRaysCommand");
    buf_.draw_command          = gfxCreateBuffer<DrawCommand>(gfx_, 1);
    buf_.draw_command.setName("DrawCommand");
    buf_.draw_indexed_command  = gfxCreateBuffer<DrawIndexedCommand>(gfx_, 1);
    buf_.draw_indexed_command.setName("DrawIndexedCommand");
    buf_.reduce_count          = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.reduce_count.setName("ReduceCount");

    buf_.debug_visualize_incident_radiance = gfxCreateBuffer<float3>(gfx_, cfg_.max_debug_visualize_incident_radiance_num_points);
    buf_.debug_visualize_incident_radiance.setName("DebugVisualizeIncidentRadiance");
    buf_.debug_visualize_incident_radiance_sum = gfxCreateBuffer<float>(gfx_, 1);
    buf_.debug_visualize_incident_radiance_sum.setName("DebugVisualizeIncidentRadianceSum");
    buf_.debug_cursor_world_pos = gfxCreateBuffer<float3>(gfx_, 1);
    buf_.debug_cursor_world_pos.setName("DebugCursorWorldPos");

    // Initialize the disk index buffer for injection
    std::vector<uint32_t> disk_index_buffer;
    for(int i = 0; i<(int)options_.SSRC_CR_disk_vertex_count - 2; i++)
    {
        disk_index_buffer.push_back(0);
        disk_index_buffer.push_back(i + 1);
        disk_index_buffer.push_back(i + 2);
    }
    buf_.disk_index_buffer     = gfxCreateBuffer<uint32_t>(gfx_, (options_.SSRC_CR_disk_vertex_count - 2) * 3, disk_index_buffer.data());
    buf_.disk_index_buffer.setName("DiskIndexBuffer");

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

    if(!initResources(capsaicin)) {
        return false;
    }

    if(!initKernels(capsaicin)) {
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

void MIGI::terminate() noexcept
{
    // Config
    cfg_ = {};
    // Free all program & kernels
    {
        gfxDestroyKernel(gfx_, kernels_.purge_tiles);
        gfxDestroyKernel(gfx_, kernels_.clear_counters);
        gfxDestroyKernel(gfx_, kernels_.clear_reservoirs);
        gfxDestroyKernel(gfx_, kernels_.generate_reservoirs);
        gfxDestroyKernel(gfx_, kernels_.compact_reservoirs);
        gfxDestroyKernel(gfx_, kernels_.resample_reservoirs);
        gfxDestroyKernel(gfx_, kernels_.populate_cells);
        gfxDestroyKernel(gfx_, kernels_.generate_update_tiles_dispatch);
        gfxDestroyKernel(gfx_, kernels_.update_tiles);
        gfxDestroyKernel(gfx_, kernels_.resolve_cells);
        gfxDestroyKernel(gfx_, kernels_.precompute_HiZ_min);
        gfxDestroyKernel(gfx_, kernels_.SSRC_clear_active_counter);
        gfxDestroyKernel(gfx_, kernels_.SSRC_reproject_and_filter);
        gfxDestroyKernel(gfx_, kernels_.SSRC_clear_tile_injection_index);
        gfxDestroyKernel(gfx_, kernels_.SSRC_inject_generate_draw_indexed);
        gfxDestroyKernel(gfx_, kernels_.SSRC_inject_reprojected_basis);
        gfxDestroyKernel(gfx_, kernels_.SSRC_clip_overflow_tile_index);
        gfxDestroyKernel(gfx_, kernels_.SSRC_allocate_extra_slot_for_basis_generation);
        gfxDestroyKernel(gfx_, kernels_.SSRC_compress_tile_basis_index);
        gfxDestroyKernel(gfx_, kernels_.SSRC_precompute_cache_update);
        gfxDestroyKernel(gfx_, kernels_.SSRC_compute_cache_update_step);
        gfxDestroyKernel(gfx_, kernels_.SSRC_normalize_cache_update);
        gfxDestroyKernel(gfx_, kernels_.SSRC_normalize_cache_update_set_reduce_count);
        gfxDestroyKernel(gfx_, kernels_.SSRC_apply_cache_update);
        gfxDestroyKernel(gfx_, kernels_.SSRC_spawn_new_basis);
        gfxDestroyKernel(gfx_, kernels_.SSRC_clip_over_allocation);
        gfxDestroyKernel(gfx_, kernels_.SSRC_integrate_ASG);
        gfxDestroyKernel(gfx_, kernels_.SSRC_reset);

        gfxDestroyKernel(gfx_, kernels_.DebugSSRC_visualize_coverage);
        gfxDestroyKernel(gfx_, kernels_.DebugSSRC_visualize_tile_occupancy);
        gfxDestroyKernel(gfx_, kernels_.DebugSSRC_basis);
        gfxDestroyKernel(gfx_, kernels_.DebugSSRC_basis_3D);
        gfxDestroyKernel(gfx_, kernels_.DebugSSRC_generate_draw_indexed);
        gfxDestroyKernel(gfx_, kernels_.DebugSSRC_show_difference);

        gfxDestroyKernel(gfx_, kernels_.generate_dispatch);
        gfxDestroyKernel(gfx_, kernels_.generate_dispatch_rays);
        gfxDestroyKernel(gfx_, kernels_.debug_hash_grid_cells);

        gfxDestroyProgram(gfx_, kernels_.program);

        kernels_ = {};
    }
    // Destroy all existing resources
    {

        gfxDestroyTexture(gfx_, tex_.cache_coverage);
        gfxDestroyTexture(gfx_, tex_.update_error_splat[0]);
        gfxDestroyTexture(gfx_, tex_.update_error_splat[1]);
        gfxDestroyTexture(gfx_, tex_.HiZ_min);
        gfxDestroyTexture(gfx_, tex_.HiZ_max);
        gfxDestroyTexture(gfx_, tex_.depth);

        gfxDestroyBuffer(gfx_, buf_.active_basis_count);
        gfxDestroyBuffer(gfx_, buf_.active_basis_index);
        gfxDestroyBuffer(gfx_, buf_.basis_effective_radius);
        gfxDestroyBuffer(gfx_, buf_.basis_film_position);
        gfxDestroyBuffer(gfx_, buf_.basis_effective_radius_film);
        gfxDestroyBuffer(gfx_, buf_.basis_location);
        gfxDestroyBuffer(gfx_, buf_.basis_parameter);
        gfxDestroyBuffer(gfx_, buf_.quantilized_basis_step);
        gfxDestroyBuffer(gfx_, buf_.basis_average_gradient_scale);
        gfxDestroyBuffer(gfx_, buf_.basis_flags);
        gfxDestroyBuffer(gfx_, buf_.free_basis_indices);
        gfxDestroyBuffer(gfx_, buf_.free_basis_indices_count);
        gfxDestroyBuffer(gfx_, buf_.tile_basis_count);
        gfxDestroyBuffer(gfx_, buf_.tile_ray_count);
        gfxDestroyBuffer(gfx_, buf_.tile_ray_offset);
        gfxDestroyBuffer(gfx_, buf_.update_ray_direction);
        gfxDestroyBuffer(gfx_, buf_.update_ray_origin);
        gfxDestroyBuffer(gfx_, buf_.update_ray_radiance_pdf);
        gfxDestroyBuffer(gfx_, buf_.update_ray_cache);
        gfxDestroyBuffer(gfx_, buf_.update_ray_count);
        gfxDestroyBuffer(gfx_, buf_.tile_update_error_sums);
        gfxDestroyBuffer(gfx_, buf_.tile_update_error);
        gfxDestroyBuffer(gfx_, buf_.tile_basis_index_injection);
        gfxDestroyBuffer(gfx_, buf_.tile_base_slot_offset);
        gfxDestroyBuffer(gfx_, buf_.tile_basis_index);
        gfxDestroyBuffer(gfx_, buf_.disk_index_buffer);
        for(const auto & i : buf_.readback)
        {
            gfxDestroyBuffer(gfx_, i);
        }

        gfxDestroyBuffer(gfx_, buf_.dispatch_count);
        gfxDestroyBuffer(gfx_, buf_.dispatch_command);
        gfxDestroyBuffer(gfx_, buf_.dispatch_rays_command);
        gfxDestroyBuffer(gfx_, buf_.draw_command);
        gfxDestroyBuffer(gfx_, buf_.draw_indexed_command);
        gfxDestroyBuffer(gfx_, buf_.reduce_count);

        tex_ = {};
        buf_ = {};
    }

    // Free SBT
    if (sbt_)
    {
        gfxDestroySbt(gfx_, sbt_);
        sbt_ = {};
    }
}

std::vector<std::string> MIGI::getShaderCompileDefinitions(const CapsaicinInternal & capsaicin) const
{
    std::vector<std::string> ret;

    ret.push_back("WAVE_SIZE=" + std::to_string(cfg_.wave_lane_count));

    auto                     light_sampler = capsaicin.getComponent<LightSamplerGridStream>();
    std::vector<std::string> light_sampler_defines(std::move(light_sampler->getShaderDefines(capsaicin)));

    for(auto e : light_sampler_defines) ret.push_back(e);

    if (capsaicin.hasAOVBuffer("OcclusionAndBentNormal")) ret.push_back("HAS_OCCLUSION");
    ret.push_back("USE_RESAMPLING");

    if(options_.enable_indirect) ret.push_back("ENABLE_INDIRECT");

    if (capsaicin.getCurrentDebugView().starts_with("HashGridCache_"))
    {
        ret.push_back("DEBUG_HASH_CELLS");
    }

    return ret;
}

}