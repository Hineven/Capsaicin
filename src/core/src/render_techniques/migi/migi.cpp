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


namespace Capsaicin
{

MIGI::MIGI()
    : RenderTechnique("MIGI"), world_space_restir_(gfx_), hash_grid_cache_(gfx_)
{}

MIGI::~MIGI() {}


void MIGI::render(CapsaicinInternal &capsaicin) noexcept {

    // Prepar settings
    updateRenderOptions(capsaicin);

    auto          light_sampler      = capsaicin.getComponent<LightSamplerGridStream>();
    auto          blue_noise_sampler = capsaicin.getComponent<BlueNoiseSampler>();
    auto          stratified_sampler = capsaicin.getComponent<StratifiedSampler>();

    const bool has_delta_lights = (GetDeltaLightCount() != 0);


    // Prepare for settings changes
    {
        if (need_reload_kernel_)
        {
            init(capsaicin);
            need_reload_kernel_ = false;
        }

        if (need_reload_hash_grid_cache_debug_view_)
        {
//            gfxDestroyKernel(gfx_, kernels_.debug_screen_probes);
            gfxDestroyKernel(gfx_, kernels_.debug_hash_grid_cells);

            GfxDrawState debug_screen_probes_draw_state;
            gfxDrawStateSetColorTarget(debug_screen_probes_draw_state, 0, capsaicin.getAOVBuffer("Debug"));

            GfxDrawState debug_hash_grid_cells_draw_state;
            gfxDrawStateSetColorTarget(debug_hash_grid_cells_draw_state, 0, capsaicin.getAOVBuffer("Debug"));
            gfxDrawStateSetDepthStencilTarget(debug_hash_grid_cells_draw_state, tex_.depth);
            gfxDrawStateSetCullMode(debug_hash_grid_cells_draw_state, D3D12_CULL_MODE_NONE);

            GfxDrawState debug_material_draw_state;
            gfxDrawStateSetColorTarget(debug_material_draw_state, 0, capsaicin.getAOVBuffer("Debug"));

//            kernels_.debug_screen_probes = gfxCreateGraphicsKernel(
//                gfx_, kernels_.program, debug_screen_probes_draw_state, "DebugScreenProbes");
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

        if (need_reset_screen_space_cache_)
        {
            clearScreenSpaceCache();
            need_reset_screen_space_cache_ = false;
        }

        // Ensure our scratch memory is allocated
        hash_grid_cache_.ensureMemoryIsAllocated(options_);
        world_space_restir_.ensureMemoryIsAllocated(options_);
    }

    GfxBuffer transform_buffer = capsaicin.getTransformBuffer();
    transform_buffer.setStride(sizeof(glm::vec4)); // this is to align with UE5 where transforms are stored as
                                                // 4x3 matrices and fetched using 3x float4 reads

    // GI10: Some NVIDIA-specific fix
    // Hineven: I don't know why this is needed, but it is.
    // Otherwise, the FetchVertices function may not work correctly.
    std::vector<GfxBuffer> vertex_buffers_;
    vertex_buffers_.resize(capsaicin.getVertexBufferCount());
    for (uint32_t i = 0; i < capsaicin.getVertexBufferCount(); ++i)
    {
        vertex_buffers_[i] = capsaicin.getVertexBuffers()[i];
        if (gfx_.getVendorId() == 0x10DEu) // NVIDIA
        {
            vertex_buffers_[i].setStride(4);
        }
    }

    // ***********************************************************
    // *          Register the program parameters                *
    // ***********************************************************

    light_sampler->addProgramParameters(capsaicin, kernels_.program);
    stratified_sampler->addProgramParameters(capsaicin, kernels_.program);
    blue_noise_sampler->addProgramParameters(capsaicin, kernels_.program);

    if (capsaicin.getMeshesUpdated() || capsaicin.getTransformsUpdated()
        || on_first_frame_)
    {
        // Update the light sampler using scene bounds
        auto sceneBounds = capsaicin.getSceneBounds();
        light_sampler->setBounds(sceneBounds, this);
    }

    on_first_frame_ = false;

    GfxTexture shading_normal_texture;
    if(options_.shading_with_geometry_normal)
        shading_normal_texture = capsaicin.getAOVBuffer("GeometryNormal");
    else shading_normal_texture = capsaicin.getAOVBuffer("ShadingNormal");

    // Global read-only textures
    gfxProgramSetParameter(gfx_, kernels_.program, "g_EnvironmentBuffer", capsaicin.getEnvironmentBuffer());
    gfxProgramSetTextures(gfx_, kernels_.program, "g_TextureMaps", capsaicin.getTextures(), capsaicin.getTextureCount());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_TextureSampler", capsaicin.getLinearSampler());

    // Geometry
    gfxProgramSetParameter(gfx_, kernels_.program, "g_IndexBuffers", capsaicin.getIndexBuffers(), capsaicin.getIndexBufferCount());
    gfxProgramSetParameter(gfx_, kernels_.program, "g_VertexBuffers", vertex_buffers_.data(), (uint32_t)vertex_buffers_.size());
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
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CameraDirection", capsaicin.getCamera().center - capsaicin.getCamera().eye);
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

    gfxProgramSetParameter(gfx_, kernels_.program, "g_Reprojection",
        glm::dmat4(camera_matrices.view_projection_prev) * glm::inverse(glm::dmat4(camera_matrices.view_projection)));

    gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousCameraPosition", previous_camera_.eye);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_FrameIndex", capsaicin.getFrameIndex());

    // G-Buffers
    gfxProgramSetParameter(gfx_, kernels_.program, "g_DepthTexture", capsaicin.getAOVBuffer("Depth"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_VisibilityTexture", capsaicin.getAOVBuffer("Visibility"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_DetailsTexture", capsaicin.getAOVBuffer("Details"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_NormalTexture", capsaicin.getAOVBuffer("Normal"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_VelocityTexture", capsaicin.getAOVBuffer("Velocity"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousDepthTexture", capsaicin.getAOVBuffer("PrevVisibilityDepth"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousNormalTexture", capsaicin.getAOVBuffer("PrevNormal"));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_PreviousDetailsTexture", capsaicin.getAOVBuffer("PrevDetails"));

    gfxProgramSetParameter(gfx_, kernels_.program, "g_PrevCombinedIlluminationTexture",capsaicin.getAOVBuffer("PrevCombinedIllumination"));

    // Indirect
    // Group size is set upon invocation
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CountBuffer", buf_.dispatch_count);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDispatchCommandBuffer", buf_.dispatch_command);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWDrawCommandBuffer", buf_.draw_command);

    // Params
    auto debug_output_aov = capsaicin.getAOVBuffer("Debug");
    gfxProgramSetTexture(gfx_, kernels_.program, "g_RWDebugOutput", debug_output_aov);
    auto gi_output_aov = capsaicin.getAOVBuffer("GlobalIllumination");
    gfxProgramSetTexture(gfx_, kernels_.program, "g_RWGlobalIlluminationOutput", gi_output_aov);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_NoImportanceSampling", (uint)options_.no_importance_sampling);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_OverWriteSGDirection", glm::normalize(options_.sg_direction));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_OverWriteSGLightPosition", options_.sg_li_position);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_OverWriteSGLambda", options_.sg_lambda);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_OverWriteSGColor", options_.sg_intensity * options_.sg_color);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_OverWriteRoughness", options_.roughness);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWBasisParameterTexture", tex_.basis_parameter);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWBasisColorTexture", tex_.basis_color);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWBasisParameterGradientTexture", tex_.basis_parameter_gradient);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWBasisColorGradientTexture", tex_.basis_color_gradient);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWRadianceXTexture", tex_.radiance_X);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWRadianceYTexture", tex_.radiance_Y);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_RadianceXTexture", tex_.radiance_X);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RadianceYTexture", tex_.radiance_Y);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWRayDirectionTexture", tex_.update_ray_direction);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWRayRadianceTexture", tex_.update_ray_radiance);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_RWRayRadianceDifferenceTexture", tex_.update_ray_radiance_difference);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_ScreenCacheDimensions", glm::int2(options_.width, options_.height));
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CacheUpdateLearningRate", options_.lr_rate);

    gfxProgramSetParameter(gfx_, kernels_.program, "g_OutputDimensions", glm::int2(options_.width, options_.height));

    // Hash grid radiance cache and world space ReSTIR: constant buffers
    {
        // Allocate and populate our constant data
        GfxBuffer hash_grid_cache_constants    = capsaicin.allocateConstantBuffer<HashGridCacheConstants>(1);
        GfxBuffer world_space_restir_constants = capsaicin.allocateConstantBuffer<WorldSpaceReSTIRConstants>(1);

        // Convert pixel size to view space size
        float cell_size = tanf(capsaicin.getCamera().fovY * options_.hash_grid_cache.cell_size
                                                                         * GFX_MAX(1.0f / options_.height,
                                                                             (float)options_.height / (options_.width * options_.width)));
        HashGridCacheConstants hash_grid_cache_constant_data = {};
        hash_grid_cache_constant_data.cell_size              = cell_size;
        hash_grid_cache_constant_data.tile_size       = cell_size * options_.hash_grid_cache.tile_cell_ratio;
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
        hash_grid_cache_constant_data.debug_mode           = options_.hash_grid_cache.debug_mode;

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

        // Bind buffers
        gfxProgramSetParameter(gfx_, kernels_.program, "g_HashGridCacheConstants", hash_grid_cache_constants);
        gfxProgramSetParameter(gfx_, kernels_.program, "g_WorldSpaceReSTIRConstants", world_space_restir_constants);
    }

    // Buffers of the hash grid cache
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

    uint32_t dispatch_size_4w[2] = {(options_.width + 3u) / 4u, (options_.height + 3u) / 4u};
    uint32_t dispatch_size_8[2] = {(options_.width + 7u) / 8u , (options_.height + 7u) / 8u};


    // Reset the cache if needed
    if(need_reset_screen_space_cache_) {
        TimedSection section_timer(*this, "Reset Screen Space Cache");
        gfxCommandBindKernel(gfx_, kernels_.reset_screen_space_cache);
        gfxCommandDispatch(gfx_, dispatch_size_8[0], dispatch_size_8[1], 1);
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
        gfxCommandBindKernel(gfx_, kernels_.trace_update_rays);
        gfxCommandDispatch(gfx_, dispatch_size_4w[0], dispatch_size_4w[1], 1);
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

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.populate_cells);
        generateDispatch(hash_grid_cache_.radiance_cache_visibility_ray_count_buffer_, num_threads[0]);

        gfxCommandBindKernel(gfx_, kernels_.populate_cells);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
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

    // Resolve our cells into the per-query storage
    {
        TimedSection const timed_section(*this, "ResolveRadianceCache");

        uint32_t const *num_threads = gfxKernelGetNumThreads(gfx_, kernels_.resolve_cells);
        generateDispatch(hash_grid_cache_.radiance_cache_visibility_ray_count_buffer_, num_threads[0]);

        gfxCommandBindKernel(gfx_, kernels_.resolve_cells);
        gfxCommandDispatchIndirect(gfx_, buf_.dispatch_command);
    }

    if(!options_.channeled_cache)
    {
        {
            const TimedSection timed_section(*this, "PrecomputeCacheUpdate");
            // Precompute cache update
            gfxCommandBindKernel(gfx_, kernels_.precompute_cache_update);
            gfxCommandDispatch(gfx_, dispatch_size_4w[0], dispatch_size_4w[1], 1);
        }
        {
            const TimedSection timed_section(*this, "UpdateCacheParameters");
            // Update cache parameters
            gfxCommandBindKernel(gfx_, kernels_.update_cache_parameters);
            gfxCommandDispatch(gfx_, dispatch_size_4w[0], dispatch_size_4w[1], 1);
        }
        {
            const TimedSection timed_section(*this, "IntegrateASG");
            gfxCommandBindKernel(gfx_, kernels_.integrate_ASG);
            gfxCommandDispatch(gfx_, dispatch_size_8[0], dispatch_size_8[1], 1);
        }
    } else {
        {
            const TimedSection timed_section(*this, "PrecomputeChanneledCacheUpdate");
            // Precompute cache update
            gfxCommandBindKernel(gfx_, kernels_.precompute_channeled_cache_update);
            gfxCommandDispatch(gfx_, dispatch_size_4w[0], dispatch_size_4w[1], 1);
        }
        {
            const TimedSection timed_section(*this, "UpdateChanneledCacheParameters");
            // Update cache parameters
            gfxCommandBindKernel(gfx_, kernels_.update_channeled_cache_params);
            gfxCommandDispatch(gfx_, dispatch_size_4w[0], dispatch_size_4w[1], 1);
        }
        {
            const TimedSection timed_section(*this, "IntegrateASGWithChanneledCache");
            gfxCommandBindKernel(gfx_, kernels_.integrate_ASG_with_channeled_cache);
            gfxCommandDispatch(gfx_, dispatch_size_4w[0], dispatch_size_4w[1], 1);
        }
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

void MIGI::clearScreenSpaceCache()
{
    TimedSection section_timer(*this, "ClearScreenSpaceCache");
    gfxCommandBindKernel(gfx_, kernels_.reset_screen_space_cache);
    auto thread_block_size = gfxKernelGetNumThreads(gfx_, kernels_.reset_screen_space_cache);
    gfxCommandDispatch(gfx_, (options_.width + thread_block_size[0] - 1) / thread_block_size[0],
        (options_.height + thread_block_size[1] - 1) / thread_block_size[1], 1);
}

void MIGI::generateDispatch(GfxBuffer dispatch_count_buffer, uint threads_per_group)
{
    gfxProgramSetParameter(gfx_, kernels_.program, "g_GroupSize", threads_per_group);
    gfxProgramSetParameter(gfx_, kernels_.program, "g_CountBuffer", dispatch_count_buffer);

    gfxCommandBindKernel(gfx_, kernels_.generate_dispatch);
    gfxCommandDispatch(gfx_, 1, 1, 1);
}

} // namespace Capsaicin
