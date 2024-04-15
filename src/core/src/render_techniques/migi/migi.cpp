/*
 * Project Capsaicin: migi.cpp
 * Created: 2024/2/27
 * This program uses MulanPSL2. See LICENSE for more.
 */

#include "capsaicin_internal.h"
#include "migi.h"

#include "components/blue_noise_sampler/blue_noise_sampler.h"
#include "components/light_sampler_grid_stream/light_sampler_grid_stream.h"
#include "components/stratified_sampler/stratified_sampler.h"

// Special hacking for manipulating the draw topology withing gfx
extern bool __override_gfx_null_render_target;
extern int  __override_gfx_null_render_target_width;
extern int  __override_gfx_null_render_target_height;


namespace Capsaicin
{

MIGI::MIGI()
    : RenderTechnique("MIGI"), world_space_restir_(gfx_), hash_grid_cache_(gfx_)
{}

MIGI::~MIGI() {terminate();}

void MIGI::render(CapsaicinInternal &capsaicin) noexcept {

    // Prepar settings
    updateRenderOptions(capsaicin);

    auto          light_sampler      = capsaicin.getComponent<LightSamplerGridStream>();
    auto          blue_noise_sampler = capsaicin.getComponent<BlueNoiseSampler>();
    auto          stratified_sampler = capsaicin.getComponent<StratifiedSampler>();

    // Prepare for settings changes
    {
        if (need_reload_kernel_)
        {
            terminate();
            init(capsaicin);
            need_reload_kernel_ = false;
        }

        if (need_reload_hash_grid_cache_debug_view_)
        {
            gfxDestroyKernel(gfx_, kernels_.debug_hash_grid_cells);

            GfxDrawState debug_screen_probes_draw_state;
            gfxDrawStateSetColorTarget(debug_screen_probes_draw_state, 0, capsaicin.getAOVBuffer("Debug"));

            GfxDrawState debug_hash_grid_cells_draw_state;
            gfxDrawStateSetColorTarget(debug_hash_grid_cells_draw_state, 0, capsaicin.getAOVBuffer("Debug"));
            gfxDrawStateSetDepthStencilTarget(debug_hash_grid_cells_draw_state, tex_.depth);
            gfxDrawStateSetCullMode(debug_hash_grid_cells_draw_state, D3D12_CULL_MODE_NONE);

            GfxDrawState debug_material_draw_state;
            gfxDrawStateSetColorTarget(debug_material_draw_state, 0, capsaicin.getAOVBuffer("Debug"));

            kernels_.debug_hash_grid_cells = gfxCreateGraphicsKernel(
                gfx_, kernels_.program, debug_hash_grid_cells_draw_state, "DebugHashGridCells");
            need_reload_hash_grid_cache_debug_view_ = false;
        }

        // Clear the hash-grid cache if user's changed the cell size
        if (need_reset_hash_grid_cache_)
        {
            clearHashGridCache(); // clear the radiance cache
            need_reset_hash_grid_cache_ = false;
        }

        // The world space reservoir size relates to the camera's field of view / resolution / restir configuration
        if(need_reset_world_space_reservoirs_) {
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

    on_first_frame_ = false;

    GfxTexture shading_normal_texture;
    if(options_.shading_with_geometry_normal)
        shading_normal_texture = capsaicin.getAOVBuffer("GeometryNormal");
    else shading_normal_texture = capsaicin.getAOVBuffer("ShadingNormal");

    // Global read-only textures
    gfxProgramSetParameter(gfx_, kernels_.program, "g_EnvironmentBuffer", capsaicin.getEnvironmentBuffer());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TextureMaps", capsaicin.getTextures(), capsaicin.getTextureCount());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TextureSampler", capsaicin.getLinearSampler());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_NearestSampler", capsaicin.getNearestSampler());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_LinearSampler", capsaicin.getLinearSampler());

    // Geometry
    gfxProgramSetParameter(gfx_, kernels_.program, "g_IndexBuffer", capsaicin.getIndexBuffer());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_VertexBuffer", capsaicin.getVertexBuffer());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_MeshBuffer", capsaicin.getMeshBuffer());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_InstanceBuffer", capsaicin.getInstanceBuffer());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_MaterialBuffer", capsaicin.getMaterialBuffer());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TransformBuffer", capsaicin.getTransformBuffer());

    // Acceleration structure
    gfxProgramSetParameter(gfx_, kernels_.program, "g_Scene", capsaicin.getAccelerationStructure());

    // Camera
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraPosition", capsaicin.getCamera().eye);
    // THIS IS  NOT NORMALIZED SOMETIMES!!!!
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraDirection", normalize(capsaicin.getCamera().center - capsaicin.getCamera().eye));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraFoVY", capsaicin.getCamera().fovY);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraFoVY2", capsaicin.getCamera().fovY * 0.5f);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_AspectRatio", capsaicin.getCamera().aspect);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraNear", capsaicin.getCamera().nearZ);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraFar", capsaicin.getCamera().farZ);
    const auto& camera = capsaicin.getCamera();
    auto camera_forward = glm::normalize(camera.center - camera.eye);
    auto camera_up = camera.up;
    auto camera_right = glm::cross(camera_forward, camera_up);
    camera_up = normalize(cross(camera_right, camera_forward));
    // Half the height of the standard camera plane
    float scale = tanf(camera.fovY / 2.f);
    float aspect = capsaicin.getCamera().aspect;
    camera_right *= scale * aspect;
    camera_up *= scale;
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraUp", camera_up);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraRight", camera_right);
    bool taa_enable = false;
    if(capsaicin.getOptions().find("taa_enable") != capsaicin.getOptions().end())
        taa_enable = std::get<bool>(capsaicin.getOptions()["taa_enable"]);
    auto const   &camera_matrices       = capsaicin.getCameraMatrices(taa_enable);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraView", camera_matrices.view);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraProjection", camera_matrices.projection);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraProjView", camera_matrices.view_projection);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraViewInv", camera_matrices.inv_view);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraProjViewInv", camera_matrices.inv_view_projection);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraPixelScale", 2.f * scale / float(options_.height));

    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reprojection",
        glm::dmat4(camera_matrices.view_projection_prev) * glm::inverse(glm::dmat4(camera_matrices.view_projection)));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_ForwardReprojection",
        glm::dmat4(camera_matrices.view_projection) * glm::inverse(glm::dmat4(camera_matrices.view_projection_prev)));

    gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousCameraPosition", previous_camera_.eye);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_FrameIndex", capsaicin.getFrameIndex());

    // G-Buffers
    gfxProgramSetParameter(gfx_, kernels_.program, "g_DepthTexture", capsaicin.getAOVBuffer("VisibilityDepth"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_VisibilityTexture", capsaicin.getAOVBuffer("Visibility"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_GeometryNormalTexture", capsaicin.getAOVBuffer("GeometryNormal"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_ShadingNormalTexture", shading_normal_texture);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_VelocityTexture", capsaicin.getAOVBuffer("Velocity"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousDepthTexture", capsaicin.getAOVBuffer("PrevVisibilityDepth"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousGeometryNormalTexture", capsaicin.getAOVBuffer("PrevGeometryNormal"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousShadingNormalTexture", capsaicin.getAOVBuffer("PrevShadingNormal"));

    gfxProgramSetParameter(gfx_, kernels_.program, "g_PrevCombinedIlluminationTexture",capsaicin.getAOVBuffer("PrevCombinedIllumination"));

    // Indirect
    // Group size is set upon invocation
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CountBuffer", buf_.dispatch_count);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDispatchCommandBuffer", buf_.dispatch_command);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDrawCommandBuffer", buf_.draw_command);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDrawIndexedCommandBuffer", buf_.draw_indexed_command);

    // Params
    auto debug_output_aov = capsaicin.getAOVBuffer("Debug");
    gfxProgramSetTexture(gfx_, kernels_.program, "g_RWDebugOutput", debug_output_aov);
    auto gi_output_aov = capsaicin.getAOVBuffer("GlobalIllumination");
    gfxProgramSetTexture(gfx_, kernels_.program, "g_RWGlobalIlluminationOutput", gi_output_aov);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWActiveBasisCountBuffer", buf_.active_basis_count);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWActiveBasisIndexBuffer", buf_.active_basis_index);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWBasisEffectiveRadiusBuffer", buf_.basis_effective_radius);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWBasisFilmPositionBuffer", buf_.basis_film_position);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWBasisScreenLambdaBuffer", buf_.basis_screen_lambda);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWBasisLocationBuffer", buf_.basis_location);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWBasisParameterBuffer", buf_.basis_parameter);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWQuantilizedBasisStepBuffer", buf_.quantilized_basis_step);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWBasisFlagsBuffer", buf_.basis_flags);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWFreeBasisIndicesBuffer", buf_.free_basis_indices);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWFreeBasisIndicesCountBuffer", buf_.free_basis_indices_count);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWTileBasisCountBuffer", buf_.tile_basis_count);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWTileBasisIndexInjectionBuffer", buf_.tile_basis_index_injection);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWTileBaseSlotOffsetBuffer", buf_.tile_base_slot_offset);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWTileBasisIndexBuffer", buf_.tile_basis_index);

    assert(options_.width % SSRC_TILE_SIZE == 0 && options_.height % SSRC_TILE_SIZE == 0);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TileDimensions", glm::int2(options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TileDimensionsInv", glm::vec2(1.f / float(options_.width / SSRC_TILE_SIZE), 1.f / float(options_.height / SSRC_TILE_SIZE)));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_BasisWInitialRadius", options_.SSRC_initial_W_radius);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_BasisSpawnCoverageThreshold", options_.SSRC_basis_spawn_coverage_threshold);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_MinWeightE", options_.SSRC_min_weight_E);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_MaxBasisCount", options_.SSRC_max_basis_count);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_CR_DiskVertexCount", options_.SSRC_CR_disk_vertex_count);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CR_DiskRadiusMultiplier", 1.f);//options_.SSRC_CR_disk_radius_multiplier);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CR_DiskRadiusBias", 0.f);//options_.SSRC_CR_disk_radius_bias);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_NoImportanceSampling", (uint)options_.no_importance_sampling);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_FixedStepSize", (uint)options_.fixed_step_size);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_UseBlueNoiseSampleDirection", (uint)options_.use_blue_noise_sample_direction);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWRayDirectionTexture", tex_.update_ray_direction);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWRayRadianceTexture", tex_.update_ray_radiance);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWRayRadianceDifferenceWSumTexture", tex_.update_ray_radiance_difference_wsum);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWCacheCoverageTexture", tex_.cache_coverage_texture);

    static_assert(SSRC_TILE_SIZE == 8);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TileHiZ_Min", tex_.HiZ_min, 2);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TileHiZ_Max", tex_.HiZ_max, 2);


    gfxProgramSetParameter(gfx_, kernels_.program, "g_ScreenCacheDimensions", glm::int2(options_.width, options_.height));

    gfxProgramSetParameter(gfx_, kernels_.program, "g_CacheUpdateLearningRate", options_.cache_update_learing_rate);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CacheUpdate_SGColor", (uint32_t)options_.cache_update_SG_color);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CacheUpdate_SGDirection", (uint32_t)options_.cache_update_SG_direction);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CacheUpdate_SGLambda", (uint32_t)options_.cache_update_SG_lambda);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CacheUpdate_WLambda", (uint32_t)options_.cache_update_W_lambda);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_OutputDimensions", glm::int2(options_.width, options_.height));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_OutputDimensionsInv", glm::vec2(1.f / float(options_.width), 1.f / float(options_.height)));

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
        hash_grid_cache_constant_data.debug_mip_level  = options_.hash_grid_cache.debug_mip_level;
        hash_grid_cache_constant_data.debug_propagate  = (uint)options_.hash_grid_cache.debug_propagate;
        hash_grid_cache_constant_data.debug_max_cell_decay = options_.hash_grid_cache.debug_max_cell_decay;
//        hash_grid_cache_constant_data.debug_bucket_occupancy_histogram_size =
//            hash_grid_cache_.debug_bucket_occupancy_histogram_size_;
//        hash_grid_cache_constant_data.debug_bucket_overflow_histogram_size =
//            hash_grid_cache_.debug_bucket_overflow_histogram_size_;
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

    gfxProgramSetParameter(gfx_, kernels_.program, "g_DebugVisualizeMode", options_.debug_visualize_mode);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_DebugVisualizeChannel", options_.debug_visualize_channel);

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


    // Reset the cache if needed
    if(need_reset_screen_space_cache_) {
        TimedSection section_timer(*this, "Reset Screen Space Cache");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_reset);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.SSRC_reset);
        uint32_t dispatch_size[] = {(options_.SSRC_max_basis_count + threads[0] - 1) / threads[0]};
        gfxCommandDispatch(gfx_, dispatch_size[0], 1, 1);
        need_reset_screen_space_cache_ = false;
    }

    // Decay and remove out-dated hash grid cache cells
    // Also clear the counters for sketch buffers
    {
        TimedSection section_timer(*this, "ClearCountersAndPurgeTiles");

        GfxBuffer radiance_cache_packed_tile_count_buffer =
            (hash_grid_cache_.radiance_cache_hash_buffer_ping_pong_
                    ? hash_grid_cache_.radiance_cache_packed_tile_count_buffer0_
                    : hash_grid_cache_.radiance_cache_packed_tile_count_buffer1_);

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.purge_tiles);
        generateDispatch(radiance_cache_packed_tile_count_buffer, num_threads[0]);

        gfxCommandBindKernel(gfx_, kernels_.clear_counters);
        gfxCommandDispatch(gfx_, 1, 1, 1);
        gfxCommandBindKernel(gfx_, kernels_.purge_tiles);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Sample and Trace update rays
    {
        TimedSection section_timer(*this, "TraceUpdateRays");
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
            gfxCommandBindKernel(gfx_, kernels_.trace_update_rays);
            gfxCommandDispatchRays(gfx_, sbt_, options_.width, options_.height, 1);
        } else
        {
            gfxCommandBindKernel(gfx_, kernels_.trace_update_rays);
            auto threads = gfxKernelGetNumThreads(gfx_, kernels_.trace_update_rays);
            assert(threads[2] == 1);
            uint32_t dispatch_size[] = {(options_.width + threads[0] - 1) / threads[0], (options_.height + threads[1] - 1) / threads[1]};
            gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
        }
    }

    // Trace results are waiting to be shaded
    // Build the light sampling cells based on shading positions from frame to frame
    light_sampler->update(capsaicin, this);

    // Clear out reservoirs in the world space reservoir hash table
    {
        TimedSection section_timer(*this, "ClearReservoirs");
        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.clear_reservoirs);
        uint32_t const  num_groups_x =
            (WorldSpaceReSTIR::kConstant_NumEntries + num_threads[0] - 1) / num_threads[0];

        gfxCommandBindKernel(gfx_, kernels_.clear_reservoirs);
        gfxCommandDispatch(gfx_, num_groups_x, 1, 1);
    }

    // Generate light sample reservoirs for our secondary hit points (from the TraceUpdateRays step)
    {
        TimedSection const timed_section(*this, "GenerateReservoirs");

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.generate_reservoirs);
        generateDispatch(hash_grid_cache_.radiance_cache_visibility_count_buffer_, num_threads[0]);

        gfxCommandBindKernel(gfx_, kernels_.generate_reservoirs);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Compact the reservoir caching structure
    {
        TimedSection const timed_section(*this, "CompactReservoirs");

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.compact_reservoirs);
        generateDispatch(world_space_restir_.reservoir_hash_list_count_buffer_, num_threads[0]);

        gfxCommandScanSum(gfx_, kGfxDataType_Uint,
            world_space_restir_
                .reservoir_hash_index_buffers_[world_space_restir_.reservoir_indirect_sample_buffer_index_],
            world_space_restir_
                .reservoir_hash_count_buffers_[world_space_restir_.reservoir_indirect_sample_buffer_index_]);
        gfxCommandBindKernel(gfx_, kernels_.compact_reservoirs);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Perform world-space reservoir reuse
    {
        TimedSection const timed_section(*this, "ResampleReservoirs");

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.resample_reservoirs);
        generateDispatch(hash_grid_cache_.radiance_cache_visibility_ray_count_buffer_, num_threads[0]);

        gfxCommandBindKernel(gfx_, kernels_.resample_reservoirs);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Trace shadow rays for the sampled lights, and update the hash grid cache cells accordingly
    // Populate the cells of our world-space hash-grid radiance cache
    {
        TimedSection const timed_section(*this, "PopulateRadianceCache");
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

            gfxCommandBindKernel(gfx_, kernels_.populate_cells);
            gfxCommandDispatchRaysIndirect(gfx_, sbt_, buf_.dispatch_rays_command);

        } else
        {
            uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.populate_cells);
            generateDispatch(hash_grid_cache_.radiance_cache_visibility_ray_count_buffer_, num_threads[0]);

            gfxCommandBindKernel(gfx_, kernels_.populate_cells);
            gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
        }
    }

    // Update our tiles using the result of the raytracing
    // (Accumulate update values to the radiance cache)
    {
        TimedSection const timed_section(*this, "UpdateRadianceCache");

        gfxCommandBindKernel(gfx_, kernels_.generate_update_tiles_dispatch);
        gfxCommandDispatch(gfx_, 1, 1, 1);

        gfxCommandBindKernel(gfx_, kernels_.update_tiles);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Resolve cells into the per-query storage
    {
        TimedSection const timed_section(*this, "ResolveRadianceCache");

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.resolve_cells);
        generateDispatch(hash_grid_cache_.radiance_cache_visibility_ray_count_buffer_, num_threads[0]);

        gfxCommandBindKernel(gfx_, kernels_.resolve_cells);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    // Precompute HiZ buffer for injection culling
    {
        auto divideAndRoundUp = [](uint32_t a, uint32_t b) -> uint32_t {
            return (a + b - 1) / b;
        };
        {
            TimedSection const timed_section(*this, "PrecomputeHiZ_Min");
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", capsaicin.getAOVBuffer("VisibilityDepth"));
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_min, 0);
            gfxCommandBindKernel(gfx_, kernels_.precompute_HiZ_min);
            auto threads = gfxKernelGetNumThreads(gfx_, kernels_.precompute_HiZ_min);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 2, threads[0]),
                divideAndRoundUp(options_.height / 2, threads[1]), 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", tex_.HiZ_min, 0);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_min, 1);
            gfxCommandBindKernel(gfx_, kernels_.precompute_HiZ_min);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 4, threads[0]),
                divideAndRoundUp(options_.height / 4, threads[1]), 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", tex_.HiZ_min, 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_min, 2);
            gfxCommandBindKernel(gfx_, kernels_.precompute_HiZ_min);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 8, threads[0]),
                divideAndRoundUp(options_.height / 8, threads[1]), 1);
        }
        {
            TimedSection const timed_section(*this, "PrecomputeHiZ_Max");
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", capsaicin.getAOVBuffer("VisibilityDepth"));
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_max, 0);
            gfxCommandBindKernel(gfx_, kernels_.precompute_HiZ_max);
            auto threads = gfxKernelGetNumThreads(gfx_, kernels_.precompute_HiZ_max);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 2, threads[0]),
                divideAndRoundUp(options_.height / 2, threads[1]), 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", tex_.HiZ_max, 0);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_max, 1);
            gfxCommandBindKernel(gfx_, kernels_.precompute_HiZ_max);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 4, threads[0]),
                divideAndRoundUp(options_.height / 4, threads[1]), 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_In", tex_.HiZ_max, 1);
            gfxProgramSetTexture(gfx_, kernels_.program, "g_RWHiZ_Out", tex_.HiZ_max, 2);
            gfxCommandBindKernel(gfx_, kernels_.precompute_HiZ_max);
            gfxCommandDispatch(gfx_, divideAndRoundUp(options_.width / 8, threads[0]),
                divideAndRoundUp(options_.height / 8, threads[1]), 1);
        }
    }

    // Clear active counter
    {
        TimedSection const timed_section(*this, "ClearActiveCounter");

        gfxCommandBindKernel(gfx_, kernels_.SSRC_clear_active_counter);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }
    // Reproject and filter SSRC from previous frame
    {
        const TimedSection timed_section(*this, "SSRC_ReprojectAndFilter");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_reproject_and_filter);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.SSRC_reproject_and_filter);
        uint32_t dispatch_size[] = {(options_.SSRC_max_basis_count + threads[0] - 1) / threads[0]};
        gfxCommandDispatch(gfx_, dispatch_size[0], 1, 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_ClearTileInjectionIndex");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_clear_tile_injection_index);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.SSRC_clear_tile_injection_index);
        assert(options_.width % SSRC_TILE_SIZE == 0 && options_.height % SSRC_TILE_SIZE == 0);
        int tile_size = options_.width / SSRC_TILE_SIZE * options_.height / SSRC_TILE_SIZE;
        uint32_t dispatch_size[] = {(tile_size + threads[0] - 1) / threads[0]};
        gfxCommandDispatch(gfx_, dispatch_size[0], 1, 1);
    }

    // Inject tiles
    {
        const TimedSection timed_section(*this, "SSRC_InjectReprojectedBasis");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_inject_generate_draw_indexed);
        gfxCommandDispatch(gfx_, 1, 1, 1);
        __override_gfx_null_render_target = true;
        __override_gfx_null_render_target_width  = int(options_.width)  / SSRC_TILE_SIZE;
        __override_gfx_null_render_target_height = int(options_.height) / SSRC_TILE_SIZE;
        gfxCommandBindKernel(gfx_, kernels_.SSRC_inject_reprojected_basis);
        gfxCommandBindIndexBuffer(gfx_, buf_.disk_index_buffer);
        gfxCommandMultiDrawIndexedIndirect(gfx_, buf_.draw_indexed_command, 1);
        __override_gfx_null_render_target = false;
    }

    {
        const TimedSection timed_section(*this, "SSRC_ClipOverflowTileIndex");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_clip_overflow_tile_index);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.SSRC_clip_overflow_tile_index);
        auto dispatch_size = options_.width * options_.height / SSRC_TILE_SIZE / SSRC_TILE_SIZE;
        dispatch_size = (dispatch_size + threads[0] - 1) / threads[0];
        gfxCommandDispatch(gfx_, dispatch_size, 1, 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_ScanSumAccumulateTileIndices");
        gfxCommandScanSum(gfx_, kGfxDataType_Uint, buf_.tile_base_slot_offset, buf_.tile_basis_count);
    }

    {
        const TimedSection timed_section(*this, "SSRC_AllocateExtraSlotForBasisGeneration");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_allocate_extra_slot_for_basis_generation);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.SSRC_allocate_extra_slot_for_basis_generation);
        auto dispatch_size = options_.width * options_.height / SSRC_TILE_SIZE / SSRC_TILE_SIZE;
        dispatch_size = (dispatch_size + threads[0] - 1) / threads[0];
        gfxCommandDispatch(gfx_, dispatch_size, 1, 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_CompressTileBasisIndex");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_compress_tile_basis_index);
        int dispatch_size[] = {int(options_.width) / SSRC_TILE_SIZE, int(options_.height) / SSRC_TILE_SIZE};
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_PrecomputeCacheUpdate");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_precompute_cache_update);
        uint32_t dispatch_size[] = {options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE};
        assert(dispatch_size[0] * SSRC_TILE_SIZE == options_.width && dispatch_size[1] * SSRC_TILE_SIZE == options_.height);
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_ComputeCacheUpdateStep");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_compute_cache_update_step);
        uint32_t dispatch_size[] = {options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE};
        assert(dispatch_size[0] * SSRC_TILE_SIZE == options_.width && dispatch_size[1] * SSRC_TILE_SIZE == options_.height);
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_ApplyCacheUpdate");
        gfxProgramSetParameter(gfx_, kernels_.program, "g_CountBuffer", buf_.active_basis_count);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.SSRC_allocate_extra_slot_for_basis_generation);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_GroupSize", threads[0]);
        gfxCommandBindKernel(gfx_, kernels_.generate_dispatch);
        gfxCommandDispatch(gfx_, 1, 1, 1);
        gfxCommandBindKernel(gfx_, kernels_.SSRC_apply_cache_update);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    {
        const TimedSection timed_section(*this, "SSRC_SpawnNewBasis");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_spawn_new_basis);
        uint32_t dispatch_size[] = {options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE};
        assert(dispatch_size[0] * SSRC_TILE_SIZE == options_.width && dispatch_size[1] * SSRC_TILE_SIZE == options_.height);
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    }

    {
        const TimedSection timed_section(*this, "SSRC_ClipOverAllocation");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_clip_over_allocation);
        gfxCommandDispatch(gfx_, 1, 1, 1);
    }

    {
        // Resolving requires the wrap sampler for material textures
        gfxProgramSetParameter(gfx_, kernels_.program, "g_TextureSampler", capsaicin.getLinearWrapSampler());

        const TimedSection timed_section(*this, "SSRC_IntegrateASG");
        gfxCommandBindKernel(gfx_, kernels_.SSRC_integrate_ASG);
        uint32_t dispatch_size[] = {options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE};
        assert(dispatch_size[0] * SSRC_TILE_SIZE == options_.width && dispatch_size[1] * SSRC_TILE_SIZE == options_.height);
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    }

    if(options_.active_debug_view == "SSRC_Coverage") {
        const TimedSection timed_section(*this, "SSRC_Coverage");
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_visualize_coverage);
        auto threads = gfxKernelGetNumThreads(gfx_, kernels_.DebugSSRC_visualize_coverage);
        uint dispatch_size[] = {(options_.width + threads[0]-1) / threads[0], (options_.height + threads[1]-1) / threads[1]};
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    } else if(options_.active_debug_view == "SSRC_TileOccupancy") {
        const TimedSection timed_section(*this, "SSRC_TileOccupancy");
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_visualize_tile_occupancy);
        uint dispatch_size[] = {options_.width / SSRC_TILE_SIZE, options_.height / SSRC_TILE_SIZE};
        gfxCommandDispatch(gfx_, dispatch_size[0], dispatch_size[1], 1);
    } else if(options_.active_debug_view == "SSRC_Basis") {
        const TimedSection timed_section(*this, "SSRC_Basis");
        gfxCommandClearTexture(gfx_, capsaicin.getAOVBuffer("Debug"));
        gfxCommandClearTexture(gfx_, tex_.depth);
        gfxCommandBindKernel(gfx_, kernels_.DebugSSRC_basis);
        gfxCommandMultiDrawIndexedIndirect(gfx_, buf_.draw_indexed_command, 1);
    }

    // Update camera history
    previous_camera_ = capsaicin.getCamera();
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

    gfxCommandBindKernel(gfx_, kernels_.generate_dispatch);
    gfxCommandDispatch(gfx_, 1, 1, 1);
}


void MIGI::generateDispatchRays(GfxBuffer count_buffer)
{
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CountBuffer", count_buffer);

    gfxCommandBindKernel(gfx_, kernels_.generate_dispatch_rays);
    gfxCommandDispatch(gfx_, 1, 1, 1);
}

} // namespace Capsaicin
