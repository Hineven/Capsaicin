/*
 * Created: 2024/7/27
 * Author:  hineven
 * See LICENSE for licensing.
 */
#include "world_cache.h"

#include "migi_internal.h"

#define PROBES_PER_ROW 128

namespace Capsaicin
{

WorldCache::WorldCache(GfxContext &gfx)
    : gfx_(gfx)
{
}

WorldCache::~WorldCache() {
    gfxDestroyBuffer(gfx_, query_count_);
    gfxDestroyBuffer(gfx_, query_visibility_);
    gfxDestroyBuffer(gfx_, query_direction);
    gfxDestroyTexture(gfx_, irradiance_2p_luminance_t_);
    gfxDestroyTexture(gfx_, momentum_t_);
    gfxDestroyTexture(gfx_, cov_t_);
    for (int i = 0; i < MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES; ++i)
    {
        gfxDestroyTexture(gfx_, probe_index_t_[i]);
    }
    gfxDestroyBuffer(gfx_, probe_spawn_request_);
    gfxDestroyBuffer(gfx_, probe_spawn_request_count_);
    gfxDestroyBuffer(gfx_, probe_header_);
    gfxDestroyBuffer(gfx_, free_probe_index_);
    gfxDestroyBuffer(gfx_, active_probe_count_);
    gfxDestroyBuffer(gfx_, active_probe_index_);
    gfxDestroyBuffer(gfx_, probe_touch_);
    gfxDestroyBuffer(gfx_, wcs_indirect_);
    gfxDestroyBuffer(gfx_, per_lane_wcs_indirect_);
    gfxDestroyBuffer(gfx_, wcap_indirect_);
    gfxDestroyBuffer(gfx_, per_lane_wcap_indirect_);
    gfxDestroyBuffer(gfx_, uv_sphere_vertex_);
    gfxDestroyBuffer(gfx_, uv_sphere_index_);
}

void WorldCache::EnsureMemoryIsAllocated(const MIGIRenderOptions &options)
{
    if(!query_count_) {
        query_count_ = gfxCreateBuffer<uint32_t>(gfx_, 1);
    }
    if (query_visibility_.getCount() != (uint32_t)options.world_cache.max_query_count)
    {
        gfxDestroyBuffer(gfx_, query_visibility_);
        gfxDestroyBuffer(gfx_, query_direction);
        query_visibility_ = gfxCreateBuffer<glm::vec4>(gfx_, options.world_cache.max_query_count);
        query_direction = gfxCreateBuffer<uint32_t>(gfx_, options.world_cache.max_query_count);
    }
    int probe_atlas_height = WORLD_CACHE_PROBE_RESOLUTION * divideAndRoundUp(options.world_cache.max_probe_count, PROBES_PER_ROW);
    int probe_atlas_width = WORLD_CACHE_PROBE_RESOLUTION * PROBES_PER_ROW;
    if (irradiance_2p_luminance_t_.getWidth() != (uint32_t)probe_atlas_width ||
        irradiance_2p_luminance_t_.getHeight() != (uint32_t)probe_atlas_height)
    {
        gfxDestroyTexture(gfx_, irradiance_2p_luminance_t_);
        irradiance_2p_luminance_t_ = gfxCreateTexture2D(
            gfx_, probe_atlas_width, probe_atlas_height,
            DXGI_FORMAT_R16G16B16A16_FLOAT);
    }
    if (momentum_t_.getWidth() != (uint32_t)probe_atlas_width ||
        momentum_t_.getHeight() != (uint32_t)probe_atlas_height)
    {
        gfxDestroyTexture(gfx_, momentum_t_);
        momentum_t_ = gfxCreateTexture2D(gfx_, probe_atlas_width, probe_atlas_height, DXGI_FORMAT_R16G16_FLOAT);
    }
    if (cov_t_.getWidth() != (uint32_t)probe_atlas_width ||
        cov_t_.getHeight() != (uint32_t)probe_atlas_height)
    {
        gfxDestroyTexture(gfx_, cov_t_);
        cov_t_ = gfxCreateTexture2D(gfx_, probe_atlas_width, probe_atlas_height, DXGI_FORMAT_R16G16_FLOAT);
    }
    for (int i = 0; i < MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES; ++i)
    {
        // -1 means 0xffffffff for uint textures in DX. See:
        // https://github.com/gpuweb/gpuweb/issues/1085#issuecomment-703858922
//        float clear_value[] = {-1, 0, 0, 0};
        // seems it just doesn't work
        if (probe_index_t_[i].getWidth() != (uint32_t)options.world_cache.clipmap_resolution ||
            probe_index_t_[i].getHeight() != (uint32_t)options.world_cache.clipmap_resolution)
        {
            gfxDestroyTexture(gfx_, probe_index_t_[i]);
            probe_index_t_[i] = gfxCreateTexture3D(gfx_,
                options.world_cache.clipmap_resolution,
                options.world_cache.clipmap_resolution,
                options.world_cache.clipmap_resolution,
                DXGI_FORMAT_R32_UINT,
                1//,
                //reinterpret_cast<const float*>(&clear_value)
            );
        }
    }
    if(probe_spawn_request_.getCount() != (uint32_t)options.world_cache.max_probe_count) {
        gfxDestroyBuffer(gfx_, probe_spawn_request_);
        probe_spawn_request_ = gfxCreateBuffer<uint32_t>(gfx_, options.world_cache.max_probe_count);
    }
    if(!probe_spawn_request_count_) {
        probe_spawn_request_count_ = gfxCreateBuffer<uint32_t>(gfx_, 1);
    }
    if(probe_header_.getCount() != (uint32_t)options.world_cache.max_probe_count) {
        gfxDestroyBuffer(gfx_, probe_header_);
        probe_header_ = gfxCreateBuffer<glm::vec4>(gfx_, options.world_cache.max_probe_count);
    }
    if(free_probe_index_.getCount() != (uint32_t)options.world_cache.max_probe_count) {
        gfxDestroyBuffer(gfx_, free_probe_index_);
        free_probe_index_ = gfxCreateBuffer<uint32_t>(gfx_, options.world_cache.max_probe_count);
    }
    if(!active_probe_count_) {
        active_probe_count_ = gfxCreateBuffer<uint32_t>(gfx_, 1);
    }
    if(active_probe_index_.getCount() != (uint32_t)options.world_cache.max_probe_count) {
        gfxDestroyBuffer(gfx_, active_probe_index_);
        active_probe_index_ = gfxCreateBuffer<uint32_t>(gfx_, options.world_cache.max_probe_count);
    }
    if(probe_touch_.getCount() != (uint32_t)options.world_cache.max_probe_count) {
        gfxDestroyBuffer(gfx_, probe_touch_);
        probe_touch_ = gfxCreateBuffer<uint32_t>(gfx_, options.world_cache.max_probe_count);
    }

    if(!wcs_indirect_) {
        wcs_indirect_ = gfxCreateBuffer<DispatchCommand>(gfx_, 1);
    }
    if(!per_lane_wcs_indirect_) {
        per_lane_wcs_indirect_ = gfxCreateBuffer<DispatchCommand>(gfx_, 1);
    }
    if(!wcap_indirect_) {
        wcap_indirect_ = gfxCreateBuffer<DispatchCommand>(gfx_, 1);
    }
    if(!per_lane_wcap_indirect_) {
        per_lane_wcap_indirect_ = gfxCreateBuffer<DispatchCommand>(gfx_, 1);
    }

    if(!uv_sphere_vertex_) {
        int vertical_res = 36;
        int horizontal_res = 72;
        std::vector<glm::vec3> vertices;
        for(int i = 0; i < vertical_res; i++) {
            for(int j = 0; j < horizontal_res; j++) {
                float theta = glm::radians(180.f * float(i) / float(vertical_res - 1));
                float phi = glm::radians(360.f * float(j) / float(horizontal_res - 1));
                glm::vec3 v = glm::vec3(
                    glm::sin(theta) * glm::cos(phi),
                    glm::cos(theta),
                    glm::sin(theta) * glm::sin(phi)
                );
                vertices.push_back(v);
            }
        }
        std::vector<uint32_t> indices;
        for(int i = 0; i < vertical_res - 1; i++) {
            for(int j = 0; j < horizontal_res - 1; j++) {
                indices.push_back(i * horizontal_res + j);
                indices.push_back(i * horizontal_res + j + 1);
                indices.push_back((i + 1) * horizontal_res + j);
                indices.push_back((i + 1) * horizontal_res + j);
                indices.push_back(i * horizontal_res + j + 1);
                indices.push_back((i + 1) * horizontal_res + j + 1);
            }
        }
        uv_sphere_vertex_ = gfxCreateBuffer<glm::vec3>(gfx_, (uint32_t)vertices.size(), vertices.data());
        uv_sphere_index_ = gfxCreateBuffer<uint32_t>(gfx_, (uint32_t)indices.size(), indices.data());
    }
}

void WorldCache::BindResources(const MIGIRenderOptions &options, GfxProgram & prog) const {
    (void) options;
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheQueryCountBuffer", query_count_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheQueryVisibilityBuffer", query_visibility_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheQueryDirectionBuffer", query_direction);

    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheIrradiance2PLuminanceTexture", irradiance_2p_luminance_t_);
    gfxProgramSetParameter(gfx_, prog, "g_WorldCacheIrradiance2PLuminanceTexture", irradiance_2p_luminance_t_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheMomentumTexture", momentum_t_);
    gfxProgramSetParameter(gfx_, prog, "g_WorldCacheMomentumTexture", momentum_t_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheCOVTexture", cov_t_);
    gfxProgramSetParameter(gfx_, prog, "g_WorldCacheCOVTexture", cov_t_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheProbeIndexTexture", probe_index_t_, MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheProbeSpawnRequestBuffer", probe_spawn_request_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheProbeSpawnRequestCountBuffer", probe_spawn_request_count_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheProbeHeaderBuffer", probe_header_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheFreeProbeIndexBuffer", free_probe_index_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheActiveProbeCountBuffer", active_probe_count_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheActiveProbeIndexBuffer", active_probe_index_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWorldCacheProbeTouchBuffer", probe_touch_);

    gfxProgramSetParameter(gfx_, prog, "g_RWWCAPDispatchCommandBuffer", wcap_indirect_);
    gfxProgramSetParameter(gfx_, prog, "g_RWPerLaneWCAPDispatchCommandBuffer", per_lane_wcap_indirect_);
    gfxProgramSetParameter(gfx_, prog, "g_RWWCSDispatchCommandBuffer", wcs_indirect_);
    gfxProgramSetParameter(gfx_, prog, "g_RWPerLaneWCSDispatchCommandBuffer", per_lane_wcs_indirect_);
}
#pragma warning(push)
// Get rid of false positive warning C6385
#pragma warning(disable:6385)
WorldCacheConstants WorldCache::UpdateConstants(MIGIRenderOptions &options, glm::vec3 camera_position) {
    WorldCacheConstants constants;
    float cascade_grid_sizes[MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES] {};
    for(int i = 0; i<options.world_cache.clipmap_levels; i++)
        cascade_grid_sizes[i] = options.world_cache.grid_size * (float)(1 << i);
    // glm::vec3 delta = camera_position - previous_constants_.VolumeMin;
    for(int i = 0; i < 3; i++) {
        for(int j = 0; j < options.world_cache.clipmap_levels; j++) {
            float abs_grid_pos = camera_position[i] - previous_constants_.VolumeMin[i];
            float grid_start = (float)previous_constants_.VolumeCascadeGridCoordOffsets[j][i] * cascade_grid_sizes[j];
            int grid_coord = (int)floor((abs_grid_pos - grid_start) / cascade_grid_sizes[j]);
            // Try to roll the cascade to keep eye in the center of all clipmaps.
            int roll = grid_coord - options.world_cache.clipmap_resolution / 2;
            constants.CascadeRolling[j][i] = roll;
        }
    }
    // The outermost level rolls VolumeMin
    int max_level = options.world_cache.clipmap_levels - 1;
    for(int i = 0; i<3; i++) {
        constants.VolumeMin[i] = previous_constants_.VolumeMin[i] + cascade_grid_sizes[max_level] * (float)constants.CascadeRolling[max_level][i];
    }
    for(int j = 0; j<options.world_cache.clipmap_levels; j++) {
        for(int i = 0; i<3; i++) {
            constants.VolumeCascadeGridCoordOffsets[j][i] =
                previous_constants_.VolumeCascadeGridCoordOffsets[j][i]
                + constants.CascadeRolling[j][i]
                - constants.CascadeRolling[max_level][i] * (1 << (max_level-j));
        }
    }
    // naive parameters
    constants.GridSize = options.world_cache.grid_size;
    constants.PreviousVolumeMin = previous_constants_.VolumeMin;
    constants.HalfGridSize = options.world_cache.grid_size * 0.5f;
    for(int i = 0; i < options.world_cache.clipmap_levels; i++) {
        constants.PreviousVolumeCascadeGridCoordOffsets[i] = previous_constants_.VolumeCascadeGridCoordOffsets[i];
    }
    constants.MaxClipmapCascades = options.world_cache.clipmap_levels;
    constants.ProbeAtlasWidth = PROBES_PER_ROW;
    constants.ProbeAtlasHeight = divideAndRoundUp(options.world_cache.max_probe_count, PROBES_PER_ROW);
    constants.NumUpdateRayPerProbe = options.world_cache.num_update_ray_per_probe;
    constants.GridCoordsBound = options.world_cache.clipmap_resolution;
    constants.ProbeScoreDecay = options.world_cache.probe_score_decay;
    constants.GridSizeInv     = 1.f / options.world_cache.grid_size;
    constants.NumProbes       = options.world_cache.max_probe_count;
    constants.ProbeInitialScore = options.world_cache.probe_initial_score;
    constants.ProbeScoreBonus = options.world_cache.probe_score_bonus;
    constants.InvAtlasDimensions = glm::vec2(1.f / constants.ProbeAtlasWidth, 1.f / constants.ProbeAtlasHeight) / float(WORLD_CACHE_PROBE_RESOLUTION);
    constants.SampleBias = options.world_cache.sample_bias;
    constants.ProbeIrradianceThreshold = options.world_cache.probe_irradiance_threshold;
    constants.ProbeLuminanceThreshold = options.world_cache.probe_luminance_threshold;

    constants.Debug_DrawProbeInstanceIndexCount = uv_sphere_index_.getCount();


    previous_constants_ = constants;
    return constants;
}
#pragma warning(pop)

void WorldCache::ClearClipmaps()
{
    assert(false);
    // dont use this, do a clear in the shader
//    for(int i = 0; i < MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES; i++) {
//        gfxCommandClearTexture(gfx_, probe_index_t_[i]);
//    }
}

}