/*
 * Project Capsaicin: migi.cpp
 * Created: 2024/2/27
 * This program uses MulanPSL2. See LICENSE for more.
 */

#include "migi.h"

#include "capsaicin_internal.h"
#include "components/blue_noise_sampler/blue_noise_sampler.h"
#include "components/brdf_lut/brdf_lut.h"
#include "components/light_sampler_grid_stream/light_sampler_grid_stream.h"
#include "components/stratified_sampler/stratified_sampler.h"
#include "migi_internal.h"

#include <fstream>
#include <functional>
#include <iostream>

// Special hacking for manipulating the draw topology withing gfx
extern bool __override_gfx_null_render_target;
extern int  __override_gfx_null_render_target_width;
extern int  __override_gfx_null_render_target_height;

extern bool __override_primitive_topology;
extern D3D_PRIMITIVE_TOPOLOGY __override_primitive_topology_draw;


GfxCamera __inspection_camera;
bool      __inspecting_probe;

bool IsInspectingProbe() {
    return __inspecting_probe;
}
GfxCamera & GetInspectionCamera () {
    return __inspection_camera;
}
namespace Capsaicin
{

MIGI::MIGI()
    : RenderTechnique("MIGI"), world_cache_(gfx_)
{}

MIGI::~MIGI() {terminate();}

// Disable C4702
#pragma warning(push)
#pragma warning(disable : 4702)
void MIGI::render(CapsaicinInternal &capsaicin) noexcept
{
    // Prepar settings
    updateRenderOptions(capsaicin);

    auto light_sampler      = capsaicin.getComponent<LightSamplerGridStream>();
    auto blue_noise_sampler = capsaicin.getComponent<BlueNoiseSampler>();
    auto stratified_sampler = capsaicin.getComponent<StratifiedSampler>();
    auto brdf_lut      = capsaicin.getComponent<BrdfLut>();

    // Prepare for settings changes
    {
        if (need_reload_kernel_)
        {
            releaseKernels();
            initKernels(capsaicin);
        }

        if(need_reload_memory_)
        {
            releaseResources();
            initResources(capsaicin);
        }

        // Make sure the frame buffers for graphics kernels are initialized before reloading them.
        if(need_reload_kernel_ || need_reload_memory_)
        {
            initGraphicsKernels(capsaicin);
            need_reset_world_cache_ = true;
            need_reset_world_space_reservoirs_ = true;
            need_reset_luts_ = true;
        }

        need_reload_kernel_ = false;
        need_reload_memory_ = false;

        // The world space reservoir size relates to the camera's field of view / resolution / restir configuration
        if (need_reset_world_space_reservoirs_)
        {
            clearReservoirs();
            need_reset_world_space_reservoirs_ = false;
        }

        // Ensure our scratch memory is allocated
        world_cache_.EnsureMemoryIsAllocated(options_);
    }

    // ***********************************************************
    // *          Register the program parameters                *
    // ***********************************************************

    light_sampler->addProgramParameters(capsaicin, kernels_.program);
    stratified_sampler->addProgramParameters(capsaicin, kernels_.program);
    blue_noise_sampler->addProgramParameters(capsaicin, kernels_.program);
    brdf_lut->addProgramParameters(capsaicin, kernels_.program);

    // Global read-only
    gfxProgramSetParameter(gfx_, kernels_.program, "g_EnvironmentBuffer", capsaicin.getEnvironmentBuffer());
    gfxProgramSetParameter(
        gfx_, kernels_.program, "g_TextureMaps", capsaicin.getTextures(), capsaicin.getTextureCount());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TextureSampler", capsaicin.getLinearSampler());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_NearestSampler", capsaicin.getNearestSampler());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_LinearSampler", capsaicin.getLinearSampler());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_ClampedPointSampler", clamped_point_sampler_);

    // Geometry
    gfxProgramSetParameter(gfx_, kernels_.program, "g_IndexBuffer", capsaicin.getIndexBuffer());

    gfxProgramSetParameter(gfx_, kernels_.program, "g_VertexBuffer", capsaicin.getVertexBuffer());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_MeshBuffer", capsaicin.getMeshBuffer());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_InstanceBuffer", capsaicin.getInstanceBuffer());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_MaterialBuffer", capsaicin.getMaterialBuffer());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TransformBuffer", capsaicin.getTransformBuffer());

    // Acceleration structure
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Scene", capsaicin.getAccelerationStructure());

    // G-Buffers
    gfxProgramSetParameter(
        gfx_, kernels_.program, "g_DepthTexture", capsaicin.getAOVBuffer("VisibilityDepth"));
    gfxProgramSetParameter(
        gfx_, kernels_.program, "g_VisibilityTexture", capsaicin.getAOVBuffer("Visibility"));
    gfxProgramSetParameter(
        gfx_, kernels_.program, "g_GeometryNormalTexture", capsaicin.getAOVBuffer("GeometryNormal"));
    gfxProgramSetParameter(
        gfx_, kernels_.program, "g_ShadingNormalTexture", capsaicin.getAOVBuffer("ShadingNormal"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_VelocityTexture", capsaicin.getAOVBuffer("Velocity"));
    gfxProgramSetParameter(
        gfx_, kernels_.program, "g_PreviousDepthTexture", capsaicin.getAOVBuffer("PrevVisibilityDepth"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousGeometryNormalTexture",
        capsaicin.getAOVBuffer("PrevGeometryNormal"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousShadingNormalTexture",
        capsaicin.getAOVBuffer("PrevShadingNormal"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_PrevCombinedIlluminationTexture",
        capsaicin.getAOVBuffer("PrevCombinedIllumination"));

    gfxProgramSetParameter(gfx_, kernels_.program, "g_BentNormalAndOcclusionTexture", capsaicin.getAOVBuffer("OcclusionAndBentNormal"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_NearFieldGlobalIlluminationTexture", capsaicin.getAOVBuffer("NearFieldGlobalIllumination"));

    // Indirect commands
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDispatchCommandBuffer", buf_.dispatch_command);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPerLaneDispatchCommandBuffer", buf_.per_lane_dispatch_command);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDispatchRaysCommandBuffer", buf_.dispatch_rays_command);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDrawCommandBuffer", buf_.draw_command);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDrawIndexedCommandBuffer", buf_.draw_indexed_command);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWReduceCountBuffer", buf_.reduce_count);

    // Outputs
    auto debug_output_aov = capsaicin.getAOVBuffer("Debug");
    gfxProgramSetTexture(gfx_, kernels_.program, "g_RWDebugOutput", debug_output_aov);
    auto gi_output_aov = capsaicin.getAOVBuffer("GlobalIllumination");
    gfxProgramSetTexture(gfx_, kernels_.program, "g_RWGlobalIlluminationOutput", gi_output_aov);

    assert(options_.width % SSRC_TILE_SIZE == 0 && options_.height % SSRC_TILE_SIZE == 0);
    static_assert(SSRC_TILE_SIZE == 16);

    // Cache datastructure
    {
        int flip = (int)internal_frame_index_ & 1;
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeHeaderPackedTexture",tex_.probe_header_packed[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeScreenCoordsTexture",tex_.probe_screen_coords[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeLinearDepthTexture", tex_.probe_linear_depth[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeWorldPositionTexture", tex_.probe_world_position[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeNormalTexture", tex_.probe_normal[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousProbeHeaderPackedTexture", tex_.probe_header_packed[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousProbeScreenCoordsTexture", tex_.probe_screen_coords[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousProbeLinearDepthTexture", tex_.probe_linear_depth[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousProbeWorldPositionTexture", tex_.probe_world_position[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousProbeNormalTexture", tex_.probe_normal[1 - flip]);

        // Color texture is rolled twice per frame, so no need for flipping.
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeColorTexture", tex_.probe_color[0]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousProbeColorTexture", tex_.probe_color[1]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeSampleColorTexture", tex_.probe_sample_color);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_ProbeSampleColorTexture", tex_.probe_sample_color);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeSHCoefficientsRTexture", tex_.probe_SH_coefficients_R);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeSHCoefficientsGTexture", tex_.probe_SH_coefficients_G);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeSHCoefficientsBTexture", tex_.probe_SH_coefficients_B);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeIrradianceTexture", tex_.probe_irradiance);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeSGBuffer", buf_.probe_SG[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousProbeSGBuffer", buf_.probe_SG[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWAllocatedProbeSGCountBuffer", buf_.allocated_probe_SG_count);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeHistoryTrustTexture", tex_.probe_history_trust);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeCompensationTexture", tex_.probe_compensation);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeUpdateRayCountBuffer", buf_.probe_update_ray_count);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeUpdateRayOffsetBuffer", buf_.probe_update_ray_offset);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWUpdateRayCountBuffer", buf_.update_ray_count);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWUpdateRayProbeBuffer", buf_.update_ray_probe);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWUpdateRayDirectionBuffer", buf_.update_ray_direction);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWUpdateRayRadianceInvPdfBuffer", buf_.update_ray_radiance_inv_pdf);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWUpdateRayLinearDepthBuffer", buf_.update_ray_linear_depth);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWTileAdaptiveProbeCountTexture", tex_.tile_adaptive_probe_count[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousTileAdaptiveProbeCountTexture", tex_.tile_adaptive_probe_count[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWNextTileAdaptiveProbeCountTexture", tex_.next_tile_adaptive_probe_count);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWTileAdaptiveProbeIndexTexture", tex_.tile_adaptive_probe_index[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousTileAdaptiveProbeIndexTexture", tex_.tile_adaptive_probe_index[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWAdaptiveProbeCountBuffer", buf_.adaptive_probe_count);
//        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeUpdateErrorBuffer", buf_.probe_update_error);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWUpdateErrorSplatTexture", tex_.update_error_splat[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_UpdateErrorSplatTexture", tex_.update_error_splat[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousUpdateErrorSplatTexture", tex_.update_error_splat[1 - flip]);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWIrradianceTexture", tex_.irradiance[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousIrradianceTexture", tex_.irradiance[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWGlossySpecularTexture", tex_.glossy_specular[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousGlossySpecularTexture", tex_.glossy_specular[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWHistoryAccumulationTexture", tex_.history_accumulation[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousHistoryAccumulationTexture", tex_.history_accumulation[1 - flip]);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousGlobalIlluminationTexture", tex_.previous_global_illumination);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDiffuseGITexture", tex_.diffuse_GI[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousDiffuseGITexture", tex_.diffuse_GI[1 - flip]);
    }

    // HiZ RWTextures are set upon kernel invocation

    gfxProgramSetParameter(gfx_, kernels_.program, "g_TileHiZ_Min", tex_.HiZ_min, 3);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TileHiZ_Max", tex_.HiZ_max, 3);

    GfxCamera camera = capsaicin.getCamera();
    if(options_.active_debug_view != "SSRC_ProbeInspection")
    {
        __inspection_camera = camera;
        __inspecting_probe = false;
    } else {
        __inspecting_probe = true;
    }

    // MI constant buffer
    MIGI_Constants C;
    {
        // THIS MUST BE NORMALIZED!
        auto camera_forward = glm::normalize(camera.center - camera.eye);
        auto camera_up = camera.up;
        auto camera_right = glm::cross(camera_forward, camera_up);
        camera_up = normalize(cross(camera_right, camera_forward));
        // Half the height of the standard camera plane
        float scale   = tanf(camera.fovY / 2.f);
        float aspect  = camera.aspect;
        camera_right *= scale * aspect;
        camera_up    *= scale;
        bool taa_enable = false;
        if(capsaicin.getOptions().find("taa_enable") != capsaicin.getOptions().end())
            taa_enable = std::get<bool>(capsaicin.getOptions()["taa_enable"]);
        CameraMatrices camera_matrices = capsaicin.getCameraMatrices(taa_enable);
        C.CameraPosition  = camera.eye;
        C.CameraFoVY      = camera.fovY;
        C.CameraDirection = camera_forward;
        C.CameraFoVY2     = camera.fovY / 2.f;
        C.AspectRatio     = aspect;
        C.CameraNear      = camera.nearZ;
        C.PreviousCameraNear = previous_camera_.nearZ;
        C.CameraFar       = camera.farZ;
        C.CameraUp        = camera_up;
        C.PreviousCameraFar = previous_camera_.farZ;
        C.CameraRight     = camera_right;
        C.CameraView      = camera_matrices.view;
        C.CameraProjView  = camera_matrices.view_projection;
        C.CameraProjViewInv = camera_matrices.inv_view_projection;
        C.CameraViewInv   = camera_matrices.inv_view;
        C.CameraPixelScale = 2.f * scale / float(options_.height);

        C.Reprojection    = camera_matrices.reprojection;
        C.ForwardReprojection = glm::dmat4(camera_matrices.view_projection) * glm::inverse(glm::dmat4(camera_matrices.view_projection_prev));
        C.PrevCameraProjView  = previous_constants_.CameraProjView;
        C.PreviousCameraPosition  = previous_camera_.eye;
        C.PreviousCameraDirection = normalize(previous_camera_.center - previous_camera_.eye);
        C.MaxBasisCount           = options_.SSRC_max_basis_count;

        C.FrameIndex = capsaicin.getFrameIndex();

        C.PreviousCameraRight     = previous_constants_.CameraRight;
        C.TileJitterFrameSeed     = options_.debug_freeze_tile_jitter ? options_.fixed_tile_jitter : C.FrameIndex;
        C.PreviousCameraUp        = previous_constants_.CameraUp;
        C.PreviousTileJitterFrameSeed = previous_constants_.TileJitterFrameSeed;

        glm::vec2 jitter          = {camera_matrices.projection[2][0], camera_matrices.projection[2][1]};
        C.TAAJitterUV             = jitter * 0.5f;
        C.PreviousTAAJitterUV     = previous_constants_.TAAJitterUV;

        C.FrameSeed  = options_.debug_freeze_frame_seed ? options_.fixed_frame_seed : C.FrameIndex;
        C.PreviousFrameSeed = previous_constants_.FrameSeed;

        C.ScreenDimensions = glm::uvec2(options_.width, options_.height);
        C.ScreenDimensionsInv = glm::vec2(1.f / options_.width, 1.f / options_.height);
        assert(options_.width % SSRC_TILE_SIZE == 0 && options_.height % SSRC_TILE_SIZE == 0);
        C.TileDimensions   = glm::uvec2(options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE);
        C.TileDimensionsInv = glm::vec2(1.f / C.TileDimensions.x, 1.f / C.TileDimensions.y);

        C.UniformScreenProbeCount = C.TileDimensions.x * C.TileDimensions.y;
        // Never used
        C.UpdateRayBudget         = 0;//options_.update_ray_budget;

        C.MaxAdaptiveProbeCount   = options_.SSRC_max_adaptive_probe_count;
        C.NoImportanceSampling    = options_.no_importance_sampling;
        C.NoAdaptiveProbes        = options_.no_adaptive_probes;
        // need_reset_screen_space_cache_ is cleared at the end of the render() function
        C.ResetCache              = need_reset_screen_space_cache_;

        C.CacheUpdateLearningRate = options_.cache_update_learing_rate;
        C.CacheUpdate_SGColor     = options_.cache_update_SG_color;
        C.CacheUpdate_SGDirection = options_.cache_update_SG_direction;
        C.CacheUpdate_SGLambda    = options_.cache_update_SG_lambda;

        C.DebugVisualizeMode      = options_.debug_visualize_mode;
        C.DebugVisualizeChannel   = options_.debug_visualize_channel;
        C.DebugVisualizeIncidentRadianceNumPoints = options_.debug_visualize_incident_radiance_num_points;

        C.DebugTonemapExposure    = 1.f;
        C.DebugCursorPixelCoords  = options_.cursor_pixel_coords;

        C.DebugLight              = options_.debug_light;

        C.UseAmbientOcclusion     = options_.ambient_occlusion;

        C.DebugLightPosition      = options_.debug_light_position;
        C.DebugLightSize          = options_.debug_light_size;
        C.DebugLightColor         = options_.debug_light_color;

        C.UseNearFieldGI          = options_.near_field_global_illumination;

        C.NoDenoiser              = options_.no_denoiser;
        C.DisableSG               = options_.disable_SG;

        C.BaseUpdateRayWaves      = options_.SSRC_base_update_ray_waves;
        C.ProbeFiltering          = !options_.no_probe_filtering;
        C.SquaredSGDirectionalRadianceWeight = options_.SSRC_squared_SG_directional_weight;
        C.SGMergingThreshold      = options_.SSRC_SG_merging_threshold;

        C.SGSimilarityAlpha       = options_.SSRC_SG_similarity_alpha;
        C.UEHemiOctahedronLutPrecomputeGroupCount = cfg_.multiprocessing_core_count;
        C.NumIcoSphereTriangles   = (uint32_t)icosphere_vertices_.size() / 3;
        C.LambdaLearningBonus     = options_.SSRC_SG_lambda_learning_bonus;

        C.Inspection_SH           = options_.Inspection_SH;
        C.Inspection_SG           = options_.Inspection_SG;
        C.Inspection_Oct          = options_.Inspection_Oct;

        C.SGColorLearningBonus    = options_.SSRC_SG_color_learning_bonus;
        C.SGDirectionLearningRate = options_.SSRC_SG_direction_learing_rate;
        C.ExcludeOctLighting      = options_.exclude_oct_lighting;
        C.ExcludeSGLighting       = options_.exclude_SG_lighting;

        glm::mat4 original_proj_view =
            glm::perspective(__inspection_camera.fovY, __inspection_camera.aspect, __inspection_camera.nearZ, __inspection_camera.farZ)
            * glm::lookAt(__inspection_camera.eye, __inspection_camera.center, __inspection_camera.up);
        C.InspectionCameraProjView = original_proj_view;

        previous_constants_ = C;

        GfxBuffer MI_constants    = capsaicin.allocateConstantBuffer<MIGI_Constants>(1);
        gfxBufferGetData<MIGI_Constants>(gfx_, MI_constants)[0] = C;
        gfxProgramSetParameter(gfx_, kernels_.program, "MI", MI_constants);
    }

    // Hash grid radiance cache and world space ReSTIR, Raytracing: constant buffers
    {
        // Allocate and populate our constant data
        GfxBuffer world_cache_constants    = capsaicin.allocateConstantBuffer<WorldCacheConstants>(1);
        GfxBuffer world_space_restir_constants = capsaicin.allocateConstantBuffer<WorldSpaceReSTIRConstants>(1);
        GfxBuffer rt_constants = capsaicin.allocateConstantBuffer<RTConstants>(1);

        // Convert pixel size to view space size
//        float          cell_size   = tanf(camera.fovY * 32
//                                                                         * GFX_MAX(1.0f / capsaicin.getHeight(),
//                                                                             (float)capsaicin.getHeight() / (capsaicin.getWidth() * capsaicin.getWidth())));
        WorldCacheConstants world_cache_constant_data =
            world_cache_.UpdateConstants(options_, camera.eye);
        // Debugging features are clipped for the hash grid cache

        gfxBufferGetData<WorldCacheConstants>(gfx_, world_cache_constants)[0] =
            world_cache_constant_data;

        RTConstants rt_constant_data = {};
        if (options_.use_dxr10)
        {
            gfxSbtGetGpuVirtualAddressRangeAndStride(gfx_, sbt_,
                (D3D12_GPU_VIRTUAL_ADDRESS_RANGE *)&rt_constant_data.ray_generation_shader_record,
                (D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE *)&rt_constant_data.miss_shader_table,
                (D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE *)&rt_constant_data.hit_group_table,
                (D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE *)&rt_constant_data.callable_shader_table);
        }

        gfxBufferGetData<RTConstants>(gfx_, rt_constants)[0] =
            rt_constant_data;

        // Bind buffers
//        gfxProgramSetParameter(gfx_, kernels_.program, "g_WorldSpaceReSTIRConstants", world_space_restir_constants);
        gfxProgramSetParameter(gfx_, kernels_.program, "WorldCache", world_cache_constants);
        gfxProgramSetParameter(gfx_, kernels_.program, "RayTracing", rt_constants);
    }

    // Lut
    gfxProgramSetParameter(gfx_, kernels_.program, "g_UEHemiOctahedronCorrectionLutTexture", tex_.UE_hemi_octahedron_correction_lut);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWUEHemiOctahedronCorrectionLutTexture", tex_.UE_hemi_octahedron_correction_lut);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWUEHemiOctahedronCorrectionLutTempBuffer", buf_.UE_hemi_octahedron_correction_lut_temp);

    // Export
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWExportBuffer", buf_.export_binary);

    // Single probe visualizing
    gfxProgramSetParameter(gfx_, kernels_.program, "g_IcoSphereVertexBuffer", buf_.icosphere_vertices);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWVisVPVNBuffer", buf_.vis_vpvn);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_VisVPVNBuffer", buf_.vis_vpvn);

    // Debugging
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDebugCursorWorldPosBuffer",
        buf_.debug_cursor_world_pos);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDebugProbeWorldPositionBuffer",
        buf_.debug_probe_world_position);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDebugVisualizeIncidentRadianceBuffer",
        buf_.debug_visualize_incident_radiance);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDebugProbeIndexBuffer", buf_.debug_probe_index);

    {
        float exposure = 1.f;
        auto & opts = capsaicin.getOptions();
        if(opts.contains("tonemap_exposure")) {
            exposure = std::get<float>(opts.at("tonemap_exposure"));
        }
        gfxProgramSetParameter(gfx_, kernels_.program, "g_DebugTonemapExposure", exposure);
    }


    // Buffers of world cache
    world_cache_.BindResources(options_, kernels_.program);

    // ***********************************************************
    // *          Render the scene                               *
    // ***********************************************************

    if(capsaicin.getFrameIndex() == 0) {
        // Clear error texture
        gfxCommandClearTexture(gfx_, tex_.update_error_splat[0]);
        gfxCommandClearTexture(gfx_, tex_.update_error_splat[1]);
        // Clear history accumulation texture
        gfxCommandClearTexture(gfx_, tex_.history_accumulation[0]);
        gfxCommandClearTexture(gfx_, tex_.history_accumulation[1]);
        need_reset_luts_ = true;
    }

    if(need_reset_luts_) {
        gfxCommandBindKernel(gfx_, kernels_.UEHemiOctahedronLutPrepare1);
        gfxCommandDispatch(gfx_, cfg_.multiprocessing_core_count, 1, 1);
        gfxCommandBindKernel(gfx_, kernels_.UEHemiOctahedronLutPrepare2);
        gfxCommandDispatch(gfx_, 1, 1, 1);
        need_reset_luts_ = false;
    }

    // Precompute HiZ buffer for injection culling and other purposes
    {
        {
            TimedSection const timed_section(*this, "PrecomputeHiZ_Min");
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", capsaicin.getAOVBuffer("VisibilityDepth"));
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_min, 0);
            gfxCommandBindKernel(gfx_, kernels_.PrecomputeHiZ_min);
            auto threads = gfxKernelGetNumThreads(gfx_, kernels_.PrecomputeHiZ_min);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 2, threads[0]),
                divideAndRoundUp(options_.height / 2, threads[1]), 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", tex_.HiZ_min, 0);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_min, 1);
            gfxCommandBindKernel(gfx_, kernels_.PrecomputeHiZ_min);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 4, threads[0]),
                divideAndRoundUp(options_.height / 4, threads[1]), 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", tex_.HiZ_min, 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_min, 2);
            gfxCommandBindKernel(gfx_, kernels_.PrecomputeHiZ_min);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 8, threads[0]),
                divideAndRoundUp(options_.height / 8, threads[1]), 1);
        }
        {
            TimedSection const timed_section(*this, "PrecomputeHiZ_Max");
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", capsaicin.getAOVBuffer("VisibilityDepth"));
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_max, 0);
            gfxCommandBindKernel(gfx_, kernels_.PrecomputeHiZ_max);
            auto threads = gfxKernelGetNumThreads(gfx_, kernels_.PrecomputeHiZ_max);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 2, threads[0]),
                divideAndRoundUp(options_.height / 2, threads[1]), 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", tex_.HiZ_max, 0);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_max, 1);
            gfxCommandBindKernel(gfx_, kernels_.PrecomputeHiZ_max);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 4, threads[0]),
                divideAndRoundUp(options_.height / 4, threads[1]), 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", tex_.HiZ_max, 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_max, 2);
            gfxCommandBindKernel(gfx_, kernels_.PrecomputeHiZ_max);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 8, threads[0]),
                divideAndRoundUp(options_.height / 8, threads[1]), 1);
        }
    }

    // Reconstruct cursor world pos for debugging purposes
    if(options_.cursor_dragging)
    {
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_FetchCursorPos);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    if(need_reset_world_cache_) {
        TimedSection const timed_section(*this, "WorldCache_Reset");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_Reset);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.WorldCache_Reset);
        int grc = divideAndRoundUp(options_.world_cache.max_probe_count, (int)threads[0]);
        gfxCommandDispatch(gfx_, grc, 1, 1);
        need_reset_world_cache_ = false;
    }

    {
        TimedSection const timed_section(*this, "WorldCache_ResetCounters");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_ResetCounters);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    // world cache : write spawn dispatch parameters
    {
        TimedSection const timed_section(*this, "WorldCache_WriteSpawnDispatchParameters");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_WriteSpawnDispatchParameters);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    // world cache : spawn probes
    {
        TimedSection const timed_section(*this, "WorldCache_SpawnProbes");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_SpawnProbes);
        gfxCommandDispatchIndirect(gfx_, world_cache_.WCSIndirectBuffer());
    }

    // world cache : recycle probes
    {
        TimedSection const timed_section(*this, "WorldCache_RecycleProbes");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_RecycleProbes);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.WorldCache_RecycleProbes);
        uint32_t dispatch_size[] = {divideAndRoundUp((uint32_t)options_.world_cache.max_probe_count, threads[0])};
        gfxCommandDispatch(gfx_, dispatch_size[0], 1, 1);
    }

    {
        TimedSection const timed_section(*this, "WorldCache_ClearClipmaps");
//        world_cache_.ClearClipmaps();
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_ClearClipmaps);
        assert(options_.world_cache.clipmap_resolution % 8 == 0);
        int stride = options_.world_cache.clipmap_resolution / 8;
        gfxCommandDispatch(gfx_, stride, stride, options_.world_cache.clipmap_resolution);
    }

    // world cache : update active list and index
    {
        TimedSection const timed_section(*this, "WorldCache_UpdateActiveListAndIndex");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_UpdateActiveListAndIndex);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.WorldCache_UpdateActiveListAndIndex);
        uint32_t dispatch_size[] = {divideAndRoundUp((uint32_t)options_.world_cache.max_probe_count, threads[0])};
        gfxCommandDispatch(gfx_, dispatch_size[0], 1, 1);
    }

    // world cache : clip counters
    {
        TimedSection const timed_section(*this, "WorldCache_ClipCounters");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_ClipCounters);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    // Clear the counter for active basis
    {
        TimedSection const timed_section(*this, "SSRC_ClearCounters");

        gfxCommandBindKernel(gfx_, kernels_.SSRC_ClearCounters);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    int uniform_probe_count = divideAndRoundUp(options_.width, SSRC_TILE_SIZE) * divideAndRoundUp(options_.height, SSRC_TILE_SIZE);

    // Reproject and filter out-dated basis from previous frame
    {
        const TimedSection timed_section(*this, "SSRC_AllocateUniformProbes");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_AllocateUniformProbes);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.SSRC_AllocateUniformProbes);
        uint32_t dispatch_size[] = {(uniform_probe_count + threads[0] - 1) / threads[0]};
        gfxCommandDispatch(gfx_, dispatch_size[0], 1, 1);
    }

    {
        static std::string section_names[SSRC_MAX_ADAPTIVE_PROBE_LAYERS];
        for(int layer = 0; layer < SSRC_MAX_ADAPTIVE_PROBE_LAYERS; layer ++)
        {
            if(section_names[layer] == "")
                section_names[layer] = std::string("SSRC_AllocateAdaptiveProbes, Layer: ") + std::to_string(layer);
            const TimedSection timed_section(*this, section_names[layer]);

            if(layer == 0) gfxCommandCopyTexture(gfx_, tex_.next_tile_adaptive_probe_count, tex_.tile_adaptive_probe_count[internal_frame_index_ & 1]);

            gfxCommandBindKernel(gfx_, kernels_.SSRC_AllocateAdaptiveProbes[layer]);
            auto     threads         = gfxKernelGetNumThreads(gfx_, kernels_.SSRC_AllocateAdaptiveProbes[layer]);
            int tile_count = divideAndRoundUp(options_.width,  SSRC_TILE_SIZE / (2 << layer))
                           * divideAndRoundUp(options_.height, SSRC_TILE_SIZE / (2 << layer));
            uint32_t dispatch_size[] = {(tile_count + threads[0] - 1) / threads[0]};
            gfxCommandDispatch(gfx_, dispatch_size[0], 1, 1);

            gfxCommandCopyTexture(gfx_, tex_.tile_adaptive_probe_count[internal_frame_index_ & 1], tex_.next_tile_adaptive_probe_count);
        }
    }

    {
        const TimedSection timed_section(*this, "SSRC_WriteProbeDispatchParameters");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_WriteProbeDispatchParameters);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    {
        const TimedSection timed_section(*this, "WorldCache_WriteProbeDispatchParameters");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_WriteProbeDispatchParameters);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    // Select a probe according to cursor position for debugging purposes
    {
        const TimedSection timed_section(*this, "DebugSSRC_SetSelectedProbe");
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_SetSelectedProbe);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_ReprojectProbeHistory");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_ReprojectProbeHistory);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Reproject update error to guide update ray allocation
    {
        const TimedSection timed_section(*this, "SSRC_ReprojectPreviousUpdateError");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_ReprojectPreviousUpdateError);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.SSRC_ReprojectPreviousUpdateError);
        uint32_t dispatch_size[] = {
            static_cast<uint32_t>(divideAndRoundUp(options_.width,  threads[0])),
            static_cast<uint32_t>(divideAndRoundUp(options_.height, threads[1]))};
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    }

    // Mipmapping the UpdateErrorSplatTexture
    {
        const TimedSection timed_section(*this, "SSRC_MipMapUpdateErrorSplat");
        gfxCommandGenerateMips(gfx_, tex_.update_error_splat[internal_frame_index_ & 1]);
    }

    {
        const TimedSection timed_section(*this, "SSRC_InitializeFailedProbes");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_InitializeFailedProbes);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    {
        const TimedSection timed_section(*this, "SSRC_AllocateUpdateRays");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_AllocateUpdateRays);
        gfxCommandDispatchIndirect(gfx_, buf_.per_lane_dispatch_command);
    }

    {
        const TimedSection timed_section(*this, "WorldCache_AllocateUpdateRays");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_AllocateUpdateRays);
        gfxCommandDispatchIndirect(gfx_, world_cache_.PerLaneWCAPIndirectBuffer());
    }

    {
        const TimedSection timed_section(*this, "MIGI_ScanSumAccumulateUpdateRayCount");
        // reduce_count is set in kernel SSRC_WriteProbeDispatchParameters
        gfxCommandScanSum(gfx_, kGfxDataType_Uint, buf_.probe_update_ray_offset, buf_.probe_update_ray_count, &buf_.reduce_count);
    }

    {
        const TimedSection timed_section(*this, "MIGI_SetUpdateRayCount");
        gfxCommandBindKernel(gfx_, kernels_.MIGI_SetUpdateRayCount);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_SampleUpdateRays");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_SampleUpdateRays);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    {
        const TimedSection timed_section(*this, "WorldCache_SampleUpdateRays");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_SampleUpdateRays);
        gfxCommandDispatchIndirect(gfx_, world_cache_.WCAPIndirectBuffer());
    }

    {
        const TimedSection timed_section(*this, "MIGI_GenerateTraceUpdateRays");
        gfxCommandBindKernel(gfx_, kernels_.MIGI_GenerateTraceUpdateRays);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    {
        const TimedSection timed_section(*this, "MIGI_TraceUpdateRaysMain");
        if(options_.use_dxr10) {
            gfxSbtSetShaderGroup(
                gfx_, sbt_, kGfxShaderGroupType_Raygen, 0, MIGIRT::kMIGICacheUpdateRaygenShaderName);
            gfxSbtSetShaderGroup(gfx_, sbt_, kGfxShaderGroupType_Miss, 0, MIGIRT::kMIGICacheUpdateMissShaderName);
            for (uint32_t i = 0; i < gfxAccelerationStructureGetRaytracingPrimitiveCount(
                                     gfx_, capsaicin.getAccelerationStructure());
                 i++)
            {
                gfxSbtSetShaderGroup(gfx_, sbt_, kGfxShaderGroupType_Hit,
                    i * capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Hit),
                    MIGIRT::kMIGICacheUpdateHitGroupName);
            }
            gfxCommandBindKernel(gfx_, kernels_.MIGI_TraceUpdateRaysMain);
            gfxCommandDispatchRaysIndirect(gfx_, sbt_, buf_.dispatch_rays_command);
        } else {
            gfxCommandBindKernel(gfx_, kernels_.MIGI_TraceUpdateRaysMain);
            gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
        }
    }

    // Trace results are waiting to be shaded
    // Build the light sampling cells based on shading positions from frame to frame
    // unused for now
    light_sampler->update(capsaicin, this);

    // Shade queries
    {
        TimedSection const timed_section(*this, "WorldCache_ShadeQueries");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_ShadeQueries);
        generateDispatch(world_cache_.QueryCountBuffer(), (uint32_t)cfg_.wave_lane_count);
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_ShadeQueries);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Update the SSRC
    {
        TimedSection const timed_section(*this, "SSRC_UpdateProbes");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_WriteProbeDispatchParameters);
        gfxCommandDispatch(gfx_, 1, 1, 1);
        gfxCommandBindKernel(gfx_, kernels_.SSRC_UpdateProbes);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Update the world cache
    {
        TimedSection const timed_section(*this, "WorldCache_UpdateProbes");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_UpdateProbes);
        gfxCommandDispatchIndirect(gfx_, world_cache_.WCAPIndirectBuffer());
    }

    // Relocate world cache probes
    {
        TimedSection const timed_section(*this, "WorldCache_MoveProbes");
        gfxCommandBindKernel(gfx_, kernels_.WorldCache_MoveProbes);
        gfxCommandDispatchIndirect(gfx_, world_cache_.PerLaneWCAPIndirectBuffer());
    }

    // Spatial filter SSRC
    {
        TimedSection const timed_section(*this, "SSRC_FilterProbes");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_FilterProbes);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Pad probe texture edges for later sampling
    {
        TimedSection const timed_section(*this, "SSRC_PadProbeTextureEdges");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_PadProbeTextureEdges);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Finally, integrate the ASG to produce global illumination
    if(!options_.DDGI_final_gather) {
        // Resolving requires the wrap sampler for material textures
        gfxProgramSetParameter(gfx_, kernels_.program, "g_TextureSampler", capsaicin.getLinearWrapSampler());
        const TimedSection timed_section(*this, "SSRC_IntegrateASG");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_IntegrateASG);
        uint32_t dispatch_size[] = {options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE};
        assert(dispatch_size[0] * SSRC_TILE_SIZE == options_.width && dispatch_size[1] * SSRC_TILE_SIZE == options_.height);
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    } else {
        // Resolving requires the wrap sampler for material textures
        gfxProgramSetParameter(gfx_, kernels_.program, "g_TextureSampler", capsaicin.getLinearWrapSampler());
        const TimedSection timed_section(*this, "SSRC_IntegrateDDGI");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_IntegrateDDGI);
        uint32_t dispatch_size[] = {options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE};
        assert(dispatch_size[0] * SSRC_TILE_SIZE == options_.width && dispatch_size[1] * SSRC_TILE_SIZE == options_.height);
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    }

    // Accumulate the update error for next frame
//    {
//        const TimedSection timed_section(*this, "SSRC_AccumulateUpdateError");
//        gfxCommandBindKernel(gfx_, kernels_.SSRC_accumulate_update_error);
//        auto num_tiles = options_.width * options_.height / SSRC_TILE_SIZE / SSRC_TILE_SIZE;
//        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.SSRC_accumulate_update_error);
//        // Use 1 thread per tile to avoid atomic operations at the cost of allocation many registers for each thread
//        gfxCommandDispatch(gfx_, divideAndRoundUp(num_tiles, threads[0]), 1, 1);
//    }

    // Denoise and copy the result to the output buffer
    {
        const TimedSection timed_section(*this, "SSRC_Denoise");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_Denoise);
        uint32_t dispatch_size[] = {options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE};
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    }

    bool camera_moved = true;
    {
        if (camera.aspect == previous_camera_.aspect && camera.center == previous_camera_.center
            && camera.eye == previous_camera_.eye && camera.farZ == previous_camera_.farZ
            && camera.fovY == previous_camera_.fovY && camera.nearZ == previous_camera_.nearZ
            && camera.type == previous_camera_.type && camera.up == previous_camera_.up)
            camera_moved = false;
    }

    // Specify whether the GI output is copied to debug drawing as a background
    bool debug_buffer_copied = false;

    if(options_.active_debug_view == "SSRC_ProbeAllocation") {
        const TimedSection timed_section(*this, "SSRC_ProbeAllocation");

        if(!debug_buffer_copied)
        {
            // Copy the depth buffer to the depth buffer for debug visualization
            gfxCommandCopyTexture(gfx_, tex_.depth, capsaicin.getAOVBuffer("VisibilityDepth"));
            gfxCommandCopyTexture(gfx_, capsaicin.getAOVBuffer("Debug"), gi_output_aov);
            debug_buffer_copied = true;
        }

        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_VisualizeProbePlacement);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.DebugSSRC_VisualizeProbePlacement);
        int tile_count = divideAndRoundUp(options_.width, SSRC_TILE_SIZE) * divideAndRoundUp(options_.height, SSRC_TILE_SIZE);
        uint32_t dispatch_size[] = {(tile_count + threads[0] - 1) / threads[0]};
        gfxCommandDispatch(gfx_, dispatch_size[0], 1, 1);
    } else if(options_.active_debug_view == "SSRC_Complexity") {
//        const TimedSection timed_section(*this, "SSRC_Complexity");
//        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_show_difference);
//        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.DebugSSRC_show_difference);
//        uint dispatch_size[] = {divideAndRoundUp(options_.width, threads[0]), divideAndRoundUp(options_.height, threads[1])};
//        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    } else if(options_.active_debug_view == "SSRC_IncidentRadiance") {
        const TimedSection timed_section(*this, "SSRC_IncidentRadiance");
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_PrepareProbeIncidentRadiance);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.DebugSSRC_PrepareProbeIncidentRadiance);
        gfxCommandDispatch(gfx_, divideAndRoundUp(options_.debug_visualize_incident_radiance_num_points, threads[0]), 1, 1);
        // Copy the depth buffer to the depth buffer for debug visualization
        if(!debug_buffer_copied)
        {
            gfxCommandCopyTexture(gfx_, tex_.depth, capsaicin.getAOVBuffer("VisibilityDepth"));
            gfxCommandCopyTexture(gfx_, capsaicin.getAOVBuffer("Debug"), gi_output_aov);
            debug_buffer_copied = true;
        }
        __override_primitive_topology = true;
        __override_primitive_topology_draw = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_VisualizeIncidentRadiance);
        gfxCommandDraw(gfx_, options_.debug_visualize_incident_radiance_num_points);
        __override_primitive_topology_draw = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_VisualizeProbeSGDirection);
        gfxCommandDraw(gfx_, 2, SSRC_MAX_NUM_BASIS_PER_PROBE);
        __override_primitive_topology = false;
        __override_primitive_topology_draw = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        // Additionally accumulate the radiance buffer to compute the numerical integral of incoming radiance
        gfxCommandReduceSum(gfx_, GfxDataType::kGfxDataType_Float, buf_.debug_visualize_incident_radiance_sum, buf_.debug_visualize_incident_radiance, &buf_.reduce_count);
    } else if(options_.active_debug_view == "SSRC_UpdateRays") {
        const TimedSection timed_section(*this, "SSRC_UpdateRays");

        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_FetchCursorPos);
        gfxCommandDispatch(gfx_, 1, 1, 1);

        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_PrepareUpdateRays);
        gfxCommandDispatch(gfx_, 1, 1, 1);
        // Copy the depth buffer to the depth buffer for debug visualization
        if(!debug_buffer_copied)
        {
            gfxCommandCopyTexture(gfx_, tex_.depth, capsaicin.getAOVBuffer("VisibilityDepth"));
            gfxCommandCopyTexture(gfx_, capsaicin.getAOVBuffer("Debug"), gi_output_aov);
            debug_buffer_copied = true;
        }
        __override_primitive_topology = true;
        __override_primitive_topology_draw = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_VisualizeUpdateRays);
        gfxCommandMultiDrawIndirect(gfx_, buf_.draw_command, 1);
        __override_primitive_topology = false;
        __override_primitive_topology_draw = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    } else if(options_.active_debug_view == "SSRC_ReprojectionTrust") {
        TimedSection timed_section(*this, "SSRC_ReprojectionTrust");
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_VisualizeReprojectionTrust);
        uint32_t dispatch_size[] = {options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE};
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    } else if(options_.active_debug_view == "SSRC_ProbeColor")
    {
        TimedSection timed_section(*this, "SSRC_ProbeColor");
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_VisualizeProbeColor);
        uint32_t dispatch_size[] = {options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE};
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    } else if(options_.active_debug_view == "WorldCache") {
        TimedSection timed_section(*this, "WorldCache_Visualize");
        // Copy the depth buffer to the depth buffer for debug visualization
        if(!debug_buffer_copied)
        {
            gfxCommandCopyTexture(gfx_, tex_.depth, capsaicin.getAOVBuffer("VisibilityDepth"));
            gfxCommandCopyTexture(gfx_, capsaicin.getAOVBuffer("Debug"), gi_output_aov);
            debug_buffer_copied = true;
        }
        gfxCommandBindKernel(gfx_, kernels_.DebugWorldCache_GenerateDraw);
        gfxCommandDispatch(gfx_, 1, 1, 1);
        gfxCommandBindKernel(gfx_, kernels_.DebugWorldCache_VisualizeProbes);
        gfxCommandBindVertexBuffer(gfx_, world_cache_.UVSphereVertexBuffer());
        gfxCommandBindIndexBuffer(gfx_, world_cache_.UVSphereIndexBuffer());
        gfxCommandMultiDrawIndexedIndirect(gfx_, buf_.draw_indexed_command, 1);
    } else if(options_.active_debug_view == "SSRC_ProbeInspection") {
        // We use a separate depth buffer
        gfxCommandClearTexture(gfx_, tex_.depth);
        if(options_.Inspection_VisualizeProbe)
        {
            gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_EvalProbe);
            int num_triangles = (uint32_t)icosphere_vertices_.size() / 3;
            gfxCommandDispatch(gfx_, divideAndRoundUp(num_triangles, cfg_.wave_lane_count), 1, 1);
            gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_VisualizeProbe);
            gfxCommandDraw(gfx_, num_triangles * 3);
        }
        if(options_.Inspection_VisualizeRays)
        {
            __override_primitive_topology = true;
            __override_primitive_topology_draw = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_VisualizeProbeRays);
            gfxCommandDraw(gfx_, 2, options_.SSRC_base_update_ray_waves * cfg_.wave_lane_count);
            __override_primitive_topology = false;
            __override_primitive_topology_draw = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }

    }

    if(options_.debug_light && options_.active_debug_view != "SSRC_ProbeInspection") {
        const TimedSection timed_section(*this, "DebugLight");
        if(!debug_buffer_copied) {
            gfxCommandCopyTexture(gfx_, tex_.depth, capsaicin.getAOVBuffer("VisibilityDepth"));
            gfxCommandCopyTexture(gfx_, capsaicin.getAOVBuffer("Debug"), gi_output_aov);
            debug_buffer_copied = true;
        }
        __override_primitive_topology = true;
        __override_primitive_topology_draw = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_VisualizeLight);
        gfxCommandDraw(gfx_, 32768);
        __override_primitive_topology = false;
        __override_primitive_topology_draw = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        if(options_.active_debug_view == "None") {
            // Copy the buffer back
            gfxCommandCopyTexture(gfx_, gi_output_aov, capsaicin.getAOVBuffer("Debug"));
        }
    }

    {
        const TimedSection timed_section(*this, "ReadBackStats");
        auto frame_index = internal_frame_index_;
        auto copy_idx = frame_index % kGfxConstant_BackBufferCount;
        assert(!readback_pending_[copy_idx]);
        gfxCommandCopyBuffer(gfx_, buf_.readback[copy_idx], 0, buf_.adaptive_probe_count, 0, sizeof(uint32_t));
        gfxCommandCopyBuffer(gfx_, buf_.readback[copy_idx], 4, buf_.allocated_probe_SG_count, 0, sizeof(float));
        gfxCommandCopyBuffer(gfx_, buf_.readback[copy_idx], 8, buf_.update_ray_count, 0, sizeof(uint32_t));
        gfxCommandCopyBuffer(gfx_, buf_.readback[copy_idx], 12, buf_.debug_visualize_incident_radiance_sum, 0, sizeof(float));
        gfxCommandCopyBuffer(gfx_, buf_.readback[copy_idx], 16, buf_.export_binary, MIGI_DEBUG_EXPORT_OFFSET_WORD * sizeof(uint32_t), sizeof(float) * 4);
        // Any values
        gfxCommandCopyBuffer(gfx_, buf_.readback[copy_idx], 32, buf_.export_binary, MIGI_DEBUG_EXPORT_OFFSET_WORD * sizeof(uint32_t) + sizeof(float) * 4, sizeof(float) * 12);
        readback_pending_[copy_idx] = true;
    }

    if(need_export_) {
        auto copy_idx = internal_frame_index_ % kGfxConstant_BackBufferCount;
        gfxCommandBindKernel(gfx_, kernels_.Export);
        gfxCommandDispatch(gfx_, 1, 1, 1);
        gfxCommandCopyBuffer(gfx_, buf_.export_staging, buf_.export_binary);
        for(auto & e : export_pending_) e = false;
        export_pending_[copy_idx] = true;
        need_export_ = false;
    }

    {
        auto frame_index = internal_frame_index_;
        auto readback_idx = (frame_index + 1) % kGfxConstant_BackBufferCount;
        if(readback_pending_[readback_idx]){
            // Readback
            auto readback_values = gfxBufferGetData<uint32_t>(gfx_, buf_.readback[readback_idx]);
            readback_values_.adaptive_probe_count = readback_values[0];
            readback_values_.allocated_probe_SG_count = readback_values[1];
            readback_values_.update_ray_count   = readback_values[2];
            readback_values_.debug_visualize_incident_irradiance = reinterpret_cast<float const *>(readback_values + 3)[0] / float(options_.debug_visualize_incident_radiance_num_points);
            for(int i = 0; i < 4; i++)
                readback_values_.reprojection_sample_probe_weights[i] = reinterpret_cast<float const *>(readback_values + 4)[i];
            for(int i = 0; i < 16; i++)
                readback_values_.anyvalues[i] = reinterpret_cast<float const *>(readback_values + 8)[i];
            readback_pending_[readback_idx] = false;
        }
        if(export_pending_[readback_idx]) {
            auto exported_binary = gfxBufferGetData(gfx_, buf_.export_staging);
            std::ofstream file("exported_data.bin", std::ios::binary);
            file.write(reinterpret_cast<char const *>(exported_binary), kExportBufferSize);
            file.close();
            export_pending_[readback_idx] = false;
            unpackExportedBinaryToMemory(exported_binary);
            printf("Exported data written to exported_data.bin\n");
        }
    }

    // Update previous global illumination
    gfxCommandCopyTexture(gfx_, tex_.previous_global_illumination, gi_output_aov);

    // Update camera history
    previous_camera_ = camera;
    // Increment internal frame index, which is different from the frame index in Capsaicin
    internal_frame_index_ ++;

    // Clear flags
    need_reset_screen_space_cache_ = false;

#ifndef NDEBUG
    fflush(stdout);
#endif
}
#pragma warning(pop)

void MIGI::unpackExportedBinaryToMemory(const void *exported_binary)
{
    // firstly, decode binary
    int rd_head = 0;
    auto rd = [&]<typename T> ()  {
        auto ret = *reinterpret_cast<const T*>(reinterpret_cast<const uint32_t*>(exported_binary) + rd_head);
        rd_head += sizeof(T);
        return ret;
    };
#define RD(type) (rd.operator()<type>())
    int ray_count = RD(int);
    vis_rays_.clear();
    for(int i = 0; i < ray_count; i++) {
        vis_rays_.push_back(RD(RayData));
    }
    int SG_count = RD(int);
    vis_sg_.clear();
    for(int i = 0; i < SG_count; i++) {
        vis_sg_.push_back(RD(SGData));
    }
    for(int i = 0; i < 27; i++) {
        vis_sh_[i] = RD(float);
    }
    for(int i = 0; i < SSRC_PROBE_TEXTURE_TEXEL_COUNT; i++) {
        vis_oct_[i/SSRC_PROBE_TEXTURE_SIZE][i%SSRC_PROBE_TEXTURE_SIZE] = RD(glm::vec3);
    }
}

void MIGI::clearReservoirs() {
    // Do nothing.
}

void MIGI::generateDispatch(GfxBuffer dispatch_count_buffer, uint threads_per_group)
{
    gfxProgramSetParameter(gfx_, kernels_.program, "g_GroupSize", threads_per_group);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CountBuffer", dispatch_count_buffer);

    gfxCommandBindKernel(gfx_, kernels_.GenerateDispatch);
    gfxCommandDispatch(gfx_, 1, 1, 1);
}


void MIGI::generateDispatchRays(GfxBuffer count_buffer)
{
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CountBuffer", count_buffer);

    gfxCommandBindKernel(gfx_, kernels_.GenerateDispatchRays);
    gfxCommandDispatch(gfx_, 1, 1, 1);
}

} // namespace Capsaicin
