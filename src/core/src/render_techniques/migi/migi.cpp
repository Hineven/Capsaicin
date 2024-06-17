/*
 * Project Capsaicin: migi.cpp
 * Created: 2024/2/27
 * This program uses MulanPSL2. See LICENSE for more.
 */

#include "migi.h"

#include "capsaicin_internal.h"
#include "components/blue_noise_sampler/blue_noise_sampler.h"
#include "components/light_sampler_grid_stream/light_sampler_grid_stream.h"
#include "components/stratified_sampler/stratified_sampler.h"
#include "migi_internal.h"

// Special hacking for manipulating the draw topology withing gfx
extern bool __override_gfx_null_render_target;
extern int  __override_gfx_null_render_target_width;
extern int  __override_gfx_null_render_target_height;

extern bool __override_primitive_topology;
extern D3D_PRIMITIVE_TOPOLOGY __override_primitive_topology_draw;


namespace Capsaicin
{

MIGI::MIGI()
    : RenderTechnique("MIGI"), world_space_restir_(gfx_), hash_grid_cache_(gfx_)
{}

MIGI::~MIGI() {terminate();}

void MIGI::render(CapsaicinInternal &capsaicin) noexcept
{
    // Prepar settings
    updateRenderOptions(capsaicin);

    auto light_sampler      = capsaicin.getComponent<LightSamplerGridStream>();
    auto blue_noise_sampler = capsaicin.getComponent<BlueNoiseSampler>();
    auto stratified_sampler = capsaicin.getComponent<StratifiedSampler>();

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
        }

        need_reload_kernel_ = false;
        need_reload_memory_ = false;


        // Clear the hash-grid cache if user's changed the cell size
        if (need_reset_hash_grid_cache_)
        {
            clearHashGridCache(); // clear the radiance cache
            need_reset_hash_grid_cache_ = false;
        }

        // The world space reservoir size relates to the camera's field of view / resolution / restir configuration
        if (need_reset_world_space_reservoirs_)
        {
            clearReservoirs();
            need_reset_world_space_reservoirs_ = false;
        }

        // Ensure our scratch memory is allocated
        hash_grid_cache_.ensureMemoryIsAllocated(options_);
        world_space_restir_.ensureMemoryIsAllocated(options_);
    }

    // ***********************************************************
    // *          Register the program parameters                *
    // ***********************************************************

    light_sampler->addProgramParameters(capsaicin, kernels_.program);
    stratified_sampler->addProgramParameters(capsaicin, kernels_.program);
    blue_noise_sampler->addProgramParameters(capsaicin, kernels_.program);

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

    // Fuck U NVIDIA

    if (gfx_.getVendorId() == 0x10DEu) // NVIDIA
    {
        capsaicin.getVertexBuffer().setStride(4);
    }



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

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeColorTexture", tex_.probe_color[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousProbeColorTexture", tex_.probe_color[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeSHCoefficientsRTexture", tex_.probe_SH_coefficients_R);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeSHCoefficientsGTexture", tex_.probe_SH_coefficients_G);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeSHCoefficientsBTexture", tex_.probe_SH_coefficients_B);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeIrradianceTexture", tex_.probe_irradiance);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeSGBuffer", buf_.probe_SG[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWPreviousProbeSGBuffer", buf_.probe_SG[1 - flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWAllocatedProbeSGCountBuffer", buf_.allocated_probe_SG_count);


        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeHistoryTrustTexture", tex_.probe_history_trust);

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
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWProbeUpdateErrorBuffer", buf_.probe_update_error);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWUpdateErrorSplatTexture", tex_.update_error_splat[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_UpdateErrorSplatTexture", tex_.update_error_splat[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousUpdateErrorSplatTexture", tex_.update_error_splat[1 - flip]);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_RWHistoryAccumulationTexture", tex_.history_accumulation[flip]);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousHistoryAccumulationTexture", tex_.history_accumulation[1 - flip]);

        gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousGlobalIlluminationTexture", tex_.previous_global_illumination);
    }

    // HiZ RWTextures are set upon kernel invocation

    gfxProgramSetParameter(gfx_, kernels_.program, "g_TileHiZ_Min", tex_.HiZ_min, 3);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TileHiZ_Max", tex_.HiZ_max, 3);

    const auto& camera = capsaicin.getCamera();
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
        float aspect  = capsaicin.getCamera().aspect;
        camera_right *= scale * aspect;
        camera_up    *= scale;
        bool taa_enable = false;
        if(capsaicin.getOptions().find("taa_enable") != capsaicin.getOptions().end())
            taa_enable = std::get<bool>(capsaicin.getOptions()["taa_enable"]);
        auto const   &camera_matrices       = capsaicin.getCameraMatrices(taa_enable);
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

        previous_constants_ = C;

        GfxBuffer MI_constants    = capsaicin.allocateConstantBuffer<MIGI_Constants>(1);
        gfxBufferGetData<MIGI_Constants>(gfx_, MI_constants)[0] = C;
        gfxProgramSetParameter(gfx_, kernels_.program, "MI", MI_constants);
    }

    // Hash grid radiance cache and world space ReSTIR, Raytracing: constant buffers
    {
        // Allocate and populate our constant data
        GfxBuffer hash_grid_cache_constants    = capsaicin.allocateConstantBuffer<HashGridCacheConstants>(1);
        GfxBuffer world_space_restir_constants = capsaicin.allocateConstantBuffer<WorldSpaceReSTIRConstants>(1);

        // Convert pixel size to view space size
        float          cell_size   = tanf(capsaicin.getCamera().fovY * options_.hash_grid_cache.cell_size
                                                                         * GFX_MAX(1.0f / capsaicin.getHeight(),
                                                                             (float)capsaicin.getHeight() / (capsaicin.getWidth() * capsaicin.getWidth())));
        HashGridCacheConstants hash_grid_cache_constant_data = {};
        hash_grid_cache_constant_data.cell_size              = cell_size;
        hash_grid_cache_constant_data.min_cell_size          = options_.hash_grid_cache.min_cell_size;
        hash_grid_cache_constant_data.tile_size       = cell_size * float(options_.hash_grid_cache.tile_cell_ratio);
        hash_grid_cache_constant_data.tile_cell_ratio = (float)options_.hash_grid_cache.tile_cell_ratio;
        hash_grid_cache_constant_data.num_buckets     = hash_grid_cache_.num_buckets_;
        hash_grid_cache_constant_data.num_cells       = hash_grid_cache_.num_cells_;
        hash_grid_cache_constant_data.num_tiles       = hash_grid_cache_.num_tiles_;
        hash_grid_cache_constant_data.num_tiles_per_bucket        = hash_grid_cache_.num_tiles_per_bucket_;
        hash_grid_cache_constant_data.size_tile_mip0              = hash_grid_cache_.size_tile_mip0_;
        hash_grid_cache_constant_data.size_tile_mip1              = hash_grid_cache_.size_tile_mip1_;
        hash_grid_cache_constant_data.size_tile_mip2              = hash_grid_cache_.size_tile_mip2_;
        hash_grid_cache_constant_data.size_tile_mip3              = hash_grid_cache_.size_tile_mip3_;
        hash_grid_cache_constant_data.num_cells_per_tile_mip0     = hash_grid_cache_.num_cells_per_tile_mip0_;
        hash_grid_cache_constant_data.num_cells_per_tile_mip1     = hash_grid_cache_.num_cells_per_tile_mip1_;
        hash_grid_cache_constant_data.num_cells_per_tile_mip2     = hash_grid_cache_.num_cells_per_tile_mip2_;
        hash_grid_cache_constant_data.num_cells_per_tile_mip3     = hash_grid_cache_.num_cells_per_tile_mip3_;
        hash_grid_cache_constant_data.num_cells_per_tile          = hash_grid_cache_.num_cells_per_tile_;
        hash_grid_cache_constant_data.first_cell_offset_tile_mip0 = hash_grid_cache_.first_cell_offset_tile_mip0_;
        hash_grid_cache_constant_data.first_cell_offset_tile_mip1 = hash_grid_cache_.first_cell_offset_tile_mip1_;
        hash_grid_cache_constant_data.first_cell_offset_tile_mip2 = hash_grid_cache_.first_cell_offset_tile_mip2_;
        hash_grid_cache_constant_data.first_cell_offset_tile_mip3 = hash_grid_cache_.first_cell_offset_tile_mip3_;
        hash_grid_cache_constant_data.buffer_ping_pong = hash_grid_cache_.radiance_cache_hash_buffer_ping_pong_;
        hash_grid_cache_constant_data.max_sample_count = options_.hash_grid_cache.max_sample_count;
        // Debugging features are clipped for the hash grid cache

        gfxBufferGetData<HashGridCacheConstants>(gfx_, hash_grid_cache_constants)[0] =
            hash_grid_cache_constant_data;

        WorldSpaceReSTIRConstants world_space_restir_constant_data = {};
        world_space_restir_constant_data.cell_size =
            tanf(capsaicin.getCamera().fovY * options_.restir.reservoir_cache_cell_size
                 * GFX_MAX(1.0f / options_.height,
                     (float)options_.height / (options_.width * options_.width)));
        world_space_restir_constant_data.num_cells            = WorldSpaceReSTIR::kConstant_NumCells;
        world_space_restir_constant_data.num_entries_per_cell = WorldSpaceReSTIR::kConstant_NumEntriesPerCell;
        gfxBufferGetData<WorldSpaceReSTIRConstants>(gfx_, world_space_restir_constants)[0] =
            world_space_restir_constant_data;

        RTConstants rt_constants = {};
        if (options_.use_dxr10)
        {
            gfxSbtGetGpuVirtualAddressRangeAndStride(gfx_, sbt_,
                (D3D12_GPU_VIRTUAL_ADDRESS_RANGE *)&rt_constants.ray_generation_shader_record,
                (D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE *)&rt_constants.miss_shader_table,
                (D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE *)&rt_constants.hit_group_table,
                (D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE *)&rt_constants.callable_shader_table);
        }

        // Bind buffers
        gfxProgramSetParameter(gfx_, kernels_.program, "g_HashGridCacheConstants", hash_grid_cache_constants);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_WorldSpaceReSTIRConstants", world_space_restir_constants);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_RTConstants", rt_constants);
    }

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
    // Buffers of the hash grid cache
    gfxProgramSetParameter(gfx_, kernels_.program, "g_HashGridCache_BuffersFloat",
        hash_grid_cache_.radiance_cache_hash_buffer_float_,
        ARRAYSIZE(hash_grid_cache_.radiance_cache_hash_buffer_float_));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_HashGridCache_BuffersUint",
        hash_grid_cache_.radiance_cache_hash_buffer_uint_,
        ARRAYSIZE(hash_grid_cache_.radiance_cache_hash_buffer_uint_));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_HashGridCache_BuffersUint2",
        hash_grid_cache_.radiance_cache_hash_buffer_uint2_,
        ARRAYSIZE(hash_grid_cache_.radiance_cache_hash_buffer_uint2_));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_HashGridCache_BuffersFloat4",
        hash_grid_cache_.radiance_cache_hash_buffer_float4_,
        ARRAYSIZE(hash_grid_cache_.radiance_cache_hash_buffer_float4_));

    // Buffers of world-space ReSTIR
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_HashBuffer",
        world_space_restir_
            .reservoir_hash_buffers_[world_space_restir_.reservoir_indirect_sample_buffer_index_]);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_HashCountBuffer",
        world_space_restir_
            .reservoir_hash_count_buffers_[world_space_restir_.reservoir_indirect_sample_buffer_index_]);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_HashIndexBuffer",
        world_space_restir_
            .reservoir_hash_index_buffers_[world_space_restir_.reservoir_indirect_sample_buffer_index_]);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_HashValueBuffer",
        world_space_restir_
            .reservoir_hash_value_buffers_[world_space_restir_.reservoir_indirect_sample_buffer_index_]);
    gfxProgramSetParameter(
        gfx_, kernels_.program, "g_Reservoir_HashListBuffer", world_space_restir_.reservoir_hash_list_buffer_);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_HashListCountBuffer",
        world_space_restir_.reservoir_hash_list_count_buffer_);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_PreviousHashBuffer",
        world_space_restir_
            .reservoir_hash_buffers_[1 - world_space_restir_.reservoir_indirect_sample_buffer_index_]);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_PreviousHashCountBuffer",
        world_space_restir_
            .reservoir_hash_count_buffers_[1 - world_space_restir_.reservoir_indirect_sample_buffer_index_]);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_PreviousHashIndexBuffer",
        world_space_restir_
            .reservoir_hash_index_buffers_[1 - world_space_restir_.reservoir_indirect_sample_buffer_index_]);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_PreviousHashValueBuffer",
        world_space_restir_
            .reservoir_hash_value_buffers_[1 - world_space_restir_.reservoir_indirect_sample_buffer_index_]);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_IndirectSampleBuffer",
        world_space_restir_.reservoir_indirect_sample_buffer_);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_IndirectSampleNormalBuffer",
        world_space_restir_.reservoir_indirect_sample_normal_buffers_
            [world_space_restir_.reservoir_indirect_sample_buffer_index_]);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_IndirectSampleMaterialBuffer",
        world_space_restir_.reservoir_indirect_sample_material_buffer_);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_IndirectSampleReservoirBuffer",
        world_space_restir_.reservoir_indirect_sample_reservoir_buffers_
            [world_space_restir_.reservoir_indirect_sample_buffer_index_]);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_PreviousIndirectSampleNormalBuffer",
        world_space_restir_.reservoir_indirect_sample_normal_buffers_
            [1 - world_space_restir_.reservoir_indirect_sample_buffer_index_]);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reservoir_PreviousIndirectSampleReservoirBuffer",
        world_space_restir_.reservoir_indirect_sample_reservoir_buffers_
            [1 - world_space_restir_.reservoir_indirect_sample_buffer_index_]);

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
    }

    // Decay and remove out-dated hash grid cache cells
    // Also clear the counters for sketch buffers
    {
        TimedSection section_timer(*this, "ClearCountersAndPurgeTiles");

        GfxBuffer radiance_cache_packed_tile_count_buffer =
            (hash_grid_cache_.radiance_cache_hash_buffer_ping_pong_
                    ? hash_grid_cache_.radiance_cache_packed_tile_count_buffer0_
                    : hash_grid_cache_.radiance_cache_packed_tile_count_buffer1_);

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.PurgeTiles);
        generateDispatch(radiance_cache_packed_tile_count_buffer, num_threads[0]);

        gfxCommandBindKernel(gfx_, kernels_.ClearCounters);
        gfxCommandDispatch(gfx_, 1, 1, 1);
        gfxCommandBindKernel(gfx_, kernels_.PurgeTiles);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
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
            static_cast<uint32_t>(divideAndRoundUp(options_.width, threads[0])),
            static_cast<uint32_t>(divideAndRoundUp(options_.height, threads[1]))};
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    }

    // Mipmapping the UpdateErrorSplatTexture
    {
        const TimedSection timed_section(*this, "SSRC_MipMapUpdateErrorSplat");
        gfxCommandGenerateMips(gfx_, tex_.update_error_splat[internal_frame_index_ & 1]);
    }

    {
        const TimedSection timed_section(*this, "SSRC_AllocateUpdateRays");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_AllocateUpdateRays);
        gfxCommandDispatchIndirect(gfx_, buf_.per_lane_dispatch_command);
    }

    {
        const TimedSection timed_section(*this, "SSRC_ScanSumAccumulateUpdateRayCount");
        // reduce_count is set in kernel SSRC_WriteProbeDispatchParameters
        gfxCommandScanSum(gfx_, kGfxDataType_Uint, buf_.probe_update_ray_offset, buf_.probe_update_ray_count, &buf_.reduce_count);
    }

    {
        const TimedSection timed_section(*this, "SSRC_SetUpdateRayCount");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_SetUpdateRayCount);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_SampleUpdateRays");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_SampleUpdateRays);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    {
        const TimedSection timed_section(*this, "SSRC_GenerateTraceUpdateRays");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_GenerateTraceUpdateRays);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_TraceUpdateRaysMain");
        if(options_.use_dxr10) {
            gfxSbtSetShaderGroup(
                gfx_, sbt_, kGfxShaderGroupType_Raygen, 0, MIGIRT::kScreenCacheUpdateRaygenShaderName);
            gfxSbtSetShaderGroup(gfx_, sbt_, kGfxShaderGroupType_Miss, 0, MIGIRT::kScreenCacheUpdateMissShaderName);
            for (uint32_t i = 0; i < gfxAccelerationStructureGetRaytracingPrimitiveCount(
                                     gfx_, capsaicin.getAccelerationStructure());
                 i++)
            {
                gfxSbtSetShaderGroup(gfx_, sbt_, kGfxShaderGroupType_Hit,
                    i * capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Hit),
                    MIGIRT::kScreenCacheUpdateHitGroupName);
            }
            gfxCommandBindKernel(gfx_, kernels_.SSRC_TraceUpdateRaysMain);
            gfxCommandDispatchRaysIndirect(gfx_, sbt_, buf_.dispatch_rays_command);
        } else {
            gfxCommandBindKernel(gfx_, kernels_.SSRC_TraceUpdateRaysMain);
            gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
        }
    }

    // Trace results are waiting to be shaded
    // Build the light sampling cells based on shading positions from frame to frame
    light_sampler->update(capsaicin, this);

    // Clear out reservoirs in the world space reservoir hash table
    {
        TimedSection section_timer(*this, "ClearReservoirs");
        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.ClearReservoirs);
        uint32_t const  num_groups_x =
            (WorldSpaceReSTIR::kConstant_NumEntries + num_threads[0] - 1) / num_threads[0];

        gfxCommandBindKernel(gfx_, kernels_.ClearReservoirs);
        gfxCommandDispatch(gfx_, num_groups_x, 1, 1);
    }

    // Generate light sample reservoirs for our secondary hit points (from the TraceUpdateRays step)
    {
        TimedSection const timed_section(*this, "GenerateReservoirs");

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.GenerateReservoirs);
        generateDispatch(hash_grid_cache_.radiance_cache_visibility_count_buffer_, num_threads[0]);

        gfxCommandBindKernel(gfx_, kernels_.GenerateReservoirs);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Compact the reservoir caching structure
    {
        TimedSection const timed_section(*this, "CompactReservoirs");

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.CompactReservoirs);
        generateDispatch(world_space_restir_.reservoir_hash_list_count_buffer_, num_threads[0]);

        gfxCommandScanSum(gfx_, kGfxDataType_Uint,
            world_space_restir_
                .reservoir_hash_index_buffers_[world_space_restir_.reservoir_indirect_sample_buffer_index_],
            world_space_restir_
                .reservoir_hash_count_buffers_[world_space_restir_.reservoir_indirect_sample_buffer_index_]);
        gfxCommandBindKernel(gfx_, kernels_.CompactReservoirs);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Perform world-space reservoir reuse
    {
        TimedSection const timed_section(*this, "ResampleReservoirs");

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.ResampleReservoirs);
        generateDispatch(hash_grid_cache_.radiance_cache_visibility_ray_count_buffer_, num_threads[0]);

        gfxCommandBindKernel(gfx_, kernels_.ResampleReservoirs);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Trace shadow rays for the sampled lights, and update the hash grid cache cells accordingly
    // Populate the cells of our world-space hash-grid radiance cache
    {
        TimedSection const timed_section(*this, "PopulateCellsMain");
        if(options_.use_dxr10) {
            gfxSbtSetShaderGroup(gfx_, sbt_, kGfxShaderGroupType_Raygen, 0, MIGIRT::kPopulateCellsRaygenShaderName);
            gfxSbtSetShaderGroup(gfx_, sbt_, kGfxShaderGroupType_Miss, 0, MIGIRT::kPopulateCellsMissShaderName);
            for (uint32_t i = 0; i < gfxAccelerationStructureGetRaytracingPrimitiveCount(
                                     gfx_, capsaicin.getAccelerationStructure());
                 i++)
            {
                gfxSbtSetShaderGroup(gfx_, sbt_, kGfxShaderGroupType_Hit,
                    i * capsaicin.getSbtStrideInEntries(kGfxShaderGroupType_Hit), MIGIRT::kPopulateCellsHitGroupName);
            }

            generateDispatchRays(hash_grid_cache_.radiance_cache_visibility_ray_count_buffer_);

            gfxCommandBindKernel(gfx_, kernels_.PopulateCellsMain);
            gfxCommandDispatchRaysIndirect(gfx_, sbt_, buf_.dispatch_rays_command);

        } else
        {
            uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.PopulateCellsMain);
            generateDispatch(hash_grid_cache_.radiance_cache_visibility_ray_count_buffer_, num_threads[0]);

            gfxCommandBindKernel(gfx_, kernels_.PopulateCellsMain);
            gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
        }
    }

    // Update our tiles using the result of the raytracing
    // (Accumulate update values to the radiance cache)
    {
        TimedSection const timed_section(*this, "UpdateTiles");

        gfxCommandBindKernel(gfx_, kernels_.GenerateUpdateTilesDispatch);
        gfxCommandDispatch(gfx_, 1, 1, 1);

        gfxCommandBindKernel(gfx_, kernels_.UpdateTiles);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Resolve cells into the per-query storage
    {
        TimedSection const timed_section(*this, "ResolveCells");

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.ResolveCells);
        generateDispatch(hash_grid_cache_.radiance_cache_visibility_ray_count_buffer_, num_threads[0]);

        gfxCommandBindKernel(gfx_, kernels_.ResolveCells);
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

    // Finally, integrate the ASG to produce global illumination
    {
        // Resolving requires the wrap sampler for material textures
        gfxProgramSetParameter(gfx_, kernels_.program, "g_TextureSampler", capsaicin.getLinearWrapSampler());
        const TimedSection timed_section(*this, "SSRC_IntegrateASG");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_IntegrateASG);
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

    // Denoiser
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
        if(options_.cursor_dragging)
        {
            gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_FetchCursorPos);
            gfxCommandDispatch(gfx_, 1, 1, 1);
        }
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
    }

    if(options_.debug_light) {
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
        readback_pending_[copy_idx] = true;
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
            readback_pending_[readback_idx] = false;
        }
    }

    // Update previous global illumination
    gfxCommandCopyTexture(gfx_, tex_.previous_global_illumination, gi_output_aov);

    // Update camera history
    previous_camera_ = capsaicin.getCamera();
    // Increment internal frame index, which is different from the frame index in Capsaicin
    internal_frame_index_ ++;

    // Clear flags
    need_reset_screen_space_cache_ = false;

#ifndef NDEBUG
    fflush(stdout);
#endif
}

void MIGI::clearHashGridCache () {
    if (hash_grid_cache_.radiance_cache_hash_buffer_)
    {
        TimedSection section_timer(*this, "ClearHashGridCache");
        gfxCommandClearBuffer(gfx_, hash_grid_cache_.radiance_cache_hash_buffer_); // clear the radiance cache
        gfxCommandClearBuffer(gfx_, hash_grid_cache_.radiance_cache_packed_tile_count_buffer0_);
        gfxCommandClearBuffer(gfx_, hash_grid_cache_.radiance_cache_packed_tile_count_buffer1_);
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
