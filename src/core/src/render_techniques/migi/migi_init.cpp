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
    cfg_ = {};
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
    // Free all program & kernels
    {
        if(kernels_.integrate_ASG) gfxDestroyKernel(gfx_, kernels_.integrate_ASG);
        if(kernels_.integrate_ASG_with_channeled_cache) gfxDestroyKernel(gfx_, kernels_.integrate_ASG_with_channeled_cache);
        if(kernels_.precompute_cache_update) gfxDestroyKernel(gfx_, kernels_.precompute_cache_update);
        if(kernels_.update_cache_parameters) gfxDestroyKernel(gfx_, kernels_.update_cache_parameters);
        if(kernels_.precompute_channeled_cache_update) gfxDestroyKernel(gfx_, kernels_.precompute_channeled_cache_update);
        if(kernels_.update_channeled_cache_params) gfxDestroyKernel(gfx_, kernels_.update_channeled_cache_params);
        if(kernels_.trace_update_rays) gfxDestroyKernel(gfx_, kernels_.trace_update_rays);
        if(kernels_.reset_screen_space_cache) gfxDestroyKernel(gfx_, kernels_.reset_screen_space_cache);
        if(kernels_.program) gfxDestroyProgram(gfx_, kernels_.program);
        memset(&kernels_, 0, sizeof(kernels_));
    }
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
        kernels_.trace_update_rays                 = gfxCreateComputeKernel(
            gfx_, kernels_.program, "TraceUpdateRays", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.clear_reservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ClearReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.generate_reservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "GenerateReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.compact_reservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "CompactReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.resample_reservoirs = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ResampleReservoirs", defines_c.data(), (uint32_t)defines_c.size());
        kernels_.populate_cells = gfxCreateComputeKernel(
            gfx_, kernels_.program, "PopulateCells", defines_c.data(), (uint32_t)defines_c.size());
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
        kernels_.reset_screen_space_cache = gfxCreateComputeKernel(
            gfx_, kernels_.program, "ResetScreenSpaceCache", defines_c.data(), (uint32_t)defines_c.size());

    }
    return true;
}

bool MIGI::initResources (const CapsaicinInternal & capsaicin) {
    // Destroy all existing resources
    {
        if(tex_.basis_parameter) gfxDestroyTexture(gfx_, tex_.basis_parameter);
        if(tex_.basis_color) gfxDestroyTexture(gfx_, tex_.basis_color);
        if(tex_.radiance_X) gfxDestroyTexture(gfx_, tex_.radiance_X);
        if(tex_.radiance_Y) gfxDestroyTexture(gfx_, tex_.radiance_Y);
        if(tex_.update_ray_direction) gfxDestroyTexture(gfx_, tex_.update_ray_direction);
        if(tex_.update_ray_radiance) gfxDestroyTexture(gfx_, tex_.update_ray_radiance);
        if(tex_.update_ray_radiance_difference) gfxDestroyTexture(gfx_, tex_.update_ray_radiance_difference);
        if(tex_.basis_color_gradient) gfxDestroyTexture(gfx_, tex_.basis_color_gradient);
        if(tex_.basis_parameter_gradient) gfxDestroyTexture(gfx_, tex_.basis_parameter_gradient);
        memset(&tex_, 0, sizeof(tex_));
    }
    tex_.basis_parameter =
        gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    tex_.basis_color =
        gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT);
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

    tex_.basis_color_gradient = gfxCreateTexture2D(
        gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
    tex_.basis_parameter_gradient = gfxCreateTexture2D(
        gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R16G16B16A16_FLOAT, 1);

    // Buffers
    buf_.dispatch_count   = gfxCreateBuffer<uint32_t>(gfx_, 1);
    buf_.dispatch_command = gfxCreateBuffer<DispatchCommand>(gfx_, 1);
    buf_.draw_command     = gfxCreateBuffer<DrawCommand>(gfx_, 1);
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

std::vector<std::string> MIGI::getShaderCompileDefinitions(const CapsaicinInternal & capsaicin) const
{
    std::vector<std::string> ret;
    auto                     light_sampler = capsaicin.getComponent<LightSamplerGridStream>();
    std::vector<std::string> light_sampler_defines(std::move(light_sampler->getShaderDefines(capsaicin)));

    ret.push_back("USE_ALPHA_TESTING");
    if (capsaicin.hasAOVBuffer("OcclusionAndBentNormal")) ret.push_back("HAS_OCCLUSION");
    ret.push_back("USE_RESAMPLING");

    if (capsaicin.getCurrentDebugView().starts_with("HashGridCache_"))
    {
        ret.push_back("DEBUG_HASH_CELLS");
    }

    return ret;
}

}