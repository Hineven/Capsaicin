/*
 * Created: 2024/7/27
 * Author:  hineven
 * See LICENSE for licensing.
 */

#ifndef CAPSAICIN_MIGI_WORLD_CACHE_H
#define CAPSAICIN_MIGI_WORLD_CACHE_H

#include "capsaicin.h"
#include "migi_common.hlsl"
#include "migi_fwd.h"

namespace Capsaicin
{
// Simple cascaded DDGI cache for world space radiance queries.
struct WorldCache
{
    WorldCache(GfxContext &gfx_);
    ~WorldCache();

    void BindResources(const MIGIRenderOptions &options, GfxProgram & prog) const;
    void EnsureMemoryIsAllocated(const MIGIRenderOptions &options);

    WorldCacheConstants UpdateConstants(MIGIRenderOptions &options, glm::vec3 camera_position);

    void ClearClipmaps ();

    inline const GfxBuffer & WCSIndirectBuffer () {return wcs_indirect_;}
    inline const GfxBuffer & WCAPIndirectBuffer () {return wcap_indirect_;}
    inline const GfxBuffer & PerLaneWCAPIndirectBuffer () {return per_lane_wcap_indirect_;}

    inline const GfxBuffer & QueryCountBuffer () {return query_count_;}
    inline const GfxBuffer & UVSphereVertexBuffer () {return uv_sphere_vertex_;}
    inline const GfxBuffer & UVSphereIndexBuffer () {return uv_sphere_index_;}
    GfxContext &gfx_;

protected:

    GfxBuffer query_count_ {};
    GfxBuffer query_visibility_ {};
    GfxBuffer query_direction {};
    GfxTexture irradiance_2p_luminance_t_ {};
    GfxTexture momentum_t_ {};
    GfxTexture cov_t_ {};
    GfxTexture probe_index_t_ [MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES] {};
    GfxBuffer probe_spawn_request_ {};
    GfxBuffer probe_spawn_request_count_ {};
    GfxBuffer probe_header_ {};
    GfxBuffer free_probe_index_ {};
    GfxBuffer active_probe_count_ {};
    GfxBuffer active_probe_index_ {};
    GfxBuffer probe_touch_ {};

    GfxBuffer wcs_indirect_ {};
    GfxBuffer per_lane_wcs_indirect_ {};
    GfxBuffer wcap_indirect_ {};
    GfxBuffer per_lane_wcap_indirect_ {};

    GfxBuffer uv_sphere_vertex_;
    GfxBuffer uv_sphere_index_;

    WorldCacheConstants previous_constants_ {};
};
}
#endif // CAPSAICIN_MIGI_WORLD_CACHE_H
