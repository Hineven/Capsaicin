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
        kernels_.generate_update_tiles_dispatch = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateUpdateTilesDispatch", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.update_tiles = gfxCreateComputeKernel(
            gfx_, kernels_.program, "UpdateTiles", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.resolve_cells = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ResolveCells", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.precompute_cache_update = gfxCreateComputeKernel(
            gfx_, kernels_.program, "PrecomputeCacheUpdate", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.update_cache_parameters = gfxCreateComputeKernel(
            gfx_, kernels_.program, "UpdateCacheParameters", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.precompute_channeled_cache_update = gfxCreateComputeKernel(gfx_, kernels_.program,
            "PrecomputeChanneledCacheUpdate", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.update_channeled_cache_params     = gfxCreateComputeKernel(gfx_, kernels_.program,
                "UpdateChanneledCacheParameters", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.integrate_ASG = gfxCreateComputeKernel(
            gfx_, kernels_.program, "IntegrateASG", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.integrate_ASG_with_channeled_cache = gfxCreateComputeKernel(gfx_, kernels_.program,
            "IntegrateASGWithChanneledCache", defines_c.data(), (uint32_t)defines_c.size());

        kernels_.generate_dispatch = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateDispatch", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.generate_dispatch_rays = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateDispatchRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.reset_screen_space_cache = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ResetScreenSpaceCache", defines_c.data(), (uint32_t)defines_c.size());

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
            kernels_.trace_update_rays = gfxCreateRaytracingKernel(gfx_, kernels_.program, nullptr, 0,
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
            GfxKernel sbt_kernels[] {kernels_.trace_update_rays, kernels_.populate_cells};
            sbt_ = gfxCreateSbt(gfx_, sbt_kernels, ARRAYSIZE(sbt_kernels), entry_count);
        }
        else
        {
            kernels_.trace_update_rays                 = gfxCreateComputeKernel(
                gfx_, kernels_.program, "TraceUpdateRaysMain", defines_c.data(), (uint32_t)defines_c.size());
            kernels_.populate_cells = gfxCreateComputeKernel(
                gfx_, kernels_.program, "PopulateCellsMain", defines_c.data(), (uint32_t)defines_c.size());
        }

    }
    return true;
}

bool MIGI::initResources (const CapsaicinInternal & capsaicin) {
    tex_.basis_parameter =
        gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.basis_color =
        gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);

    tex_.basis_parameter_gradient = gfxCreateTexture2D(
        gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
    tex_.basis_color_gradient = gfxCreateTexture2D(
        gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT, 1);

    tex_.radiance_X =
        gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.radiance_Y =
        gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.update_ray_direction =
        gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R8G8B8A8_UNORM, 1);
    tex_.update_ray_radiance = gfxCreateTexture2D(
        gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
    tex_.update_ray_radiance_difference = gfxCreateTexture2D(
        gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT, 1);

    tex_.depth = gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_D32_FLOAT);

    // Buffers
    buf_.dispatch_count        = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.dispatch_command      = gfxCreateBuffer<DispatchCommand>(gfx_, 1);
    buf_.dispatch_rays_command = gfxCreateBuffer<DispatchRaysCommand>(gfx_, 1);
    buf_.draw_command          = gfxCreateBuffer<DrawCommand>(gfx_, 1);
    return true;
}


bool MIGI::init(const CapsaicinInternal &capsaicin) noexcept
{
    if(!initConfig(capsaicin))
    {
        return false;
    }
    if(!initKernels(capsaicin)) {
        return false;
    }
    if(!initResources(capsaicin)) {
        return false;
    }

    auto light_sampler = capsaicin.getComponent<LightSamplerGridStream>();
    light_sampler->reserveBoundsValues(capsaicin.getWidth() * capsaicin.getHeight(), this);
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
        gfxDestroyKernel(gfx_, kernels_.precompute_cache_update);
        gfxDestroyKernel(gfx_, kernels_.update_cache_parameters);
        gfxDestroyKernel(gfx_, kernels_.precompute_channeled_cache_update);
        gfxDestroyKernel(gfx_, kernels_.update_channeled_cache_params);
        gfxDestroyKernel(gfx_, kernels_.integrate_ASG);
        gfxDestroyKernel(gfx_, kernels_.integrate_ASG_with_channeled_cache);
        gfxDestroyKernel(gfx_, kernels_.generate_dispatch);
        gfxDestroyKernel(gfx_, kernels_.generate_dispatch_rays);
        gfxDestroyKernel(gfx_, kernels_.reset_screen_space_cache);
        gfxDestroyKernel(gfx_, kernels_.debug_hash_grid_cells);

        gfxDestroyProgram(gfx_, kernels_.program);

        kernels_ = {};
    }
    // Destroy all existing resources
    {
        gfxDestroyTexture(gfx_, tex_.basis_parameter);
        gfxDestroyTexture(gfx_, tex_.basis_color);
        gfxDestroyTexture(gfx_, tex_.basis_parameter_gradient);
        gfxDestroyTexture(gfx_, tex_.basis_color_gradient);
        gfxDestroyTexture(gfx_, tex_.radiance_X);
        gfxDestroyTexture(gfx_, tex_.radiance_Y);
        gfxDestroyTexture(gfx_, tex_.update_ray_direction);
        gfxDestroyTexture(gfx_, tex_.update_ray_radiance);
        gfxDestroyTexture(gfx_, tex_.update_ray_radiance_difference);
        gfxDestroyTexture(gfx_, tex_.depth);

        gfxDestroyBuffer(gfx_, buf_.dispatch_count);
        gfxDestroyBuffer(gfx_, buf_.dispatch_command);
        gfxDestroyBuffer(gfx_, buf_.dispatch_rays_command);
        gfxDestroyBuffer(gfx_, buf_.draw_command);

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