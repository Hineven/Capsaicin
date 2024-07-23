#ifndef MIGI_COMMON_HLSL
#define MIGI_COMMON_HLSL

#include "../../gpu_shared.h"

// The float4x4 typedef will pollute the CUDA namespace.
#ifdef __CUDA_ARCH__
#error "This file is not compatiable with CUDA."
#endif
#ifdef __cplusplus
namespace Capsaicin {
#endif

#define WORLD_CACHE_PROBE_RESOLUTION 6

#define MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES 4
#if MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES > 4
#error "MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES > 4."
#endif

struct WorldCacheConstants {
    float3 CascadeCenters[MIGI_WORLDCACHE_MAX_CLIPMAP_CASCADES];
    float  GridSize;
    float  HalfGridSize;
    uint   MaxClipmapCascades;
    // In probes
    uint   ProbeAtlasWidth;
    // In probes
    uint   ProbeAtlasHeight;
    uint   NumUpdateRayPerProbe;

    // GridCoordsBound * 2 - 1 == Clipmap resolution
    // It has to be no greater than 64
    uint    GridCoordsBound;

    uint   ProbeScoreDecay;
};

struct WorldSpaceReSTIRConstants
{
    float cell_size;
    uint  num_cells;
    uint  num_entries_per_cell;
    uint  unused_padding;
};

struct RTConstants
{
    GpuVirtualAddressRange          ray_generation_shader_record;
    GpuVirtualAddressRangeAndStride miss_shader_table;
    uint2                           padding0;
    GpuVirtualAddressRangeAndStride hit_group_table;
    uint2                           padding1;
    GpuVirtualAddressRangeAndStride callable_shader_table;
    uint2                           padding2;
};

struct MIGI_Constants {
    
    // Common view parameters
    float3   CameraPosition;
    float    CameraFoVY;//
    float3   CameraDirection;
    float    CameraFoVY2;//
    float    AspectRatio;
    float    CameraNear;
    float    PreviousCameraNear;
    float    CameraFar;//
    float3   CameraUp;
    float    PreviousCameraFar;//
    float3   CameraRight;
    // The scale of a single pixel in the standard camera plane (z = 1)
    float    CameraPixelScale;//
    float4x4 CameraView;
    float4x4 CameraProjView;
    float4x4 CameraViewInv;
    float4x4 CameraProjViewInv;

    // Note: NDC in MIGI means [-1, 1] x [-1, 1] x [0, 1], and 1 stands for the far plane.
    // Current NDC -> Prev NDC
    float4x4 Reprojection;
    // Prev    NDC -> Current NDC
    float4x4 ForwardReprojection;

    float4x4 PrevCameraProjView;

    float3   PreviousCameraPosition;

    uint FrameIndex;//

    float3 PreviousCameraRight;
    uint TileJitterFrameSeed;//
    float3 PreviousCameraUp;
    uint PreviousTileJitterFrameSeed;//

    // Add these values to UV when using camera coordinate system to recover pixel world positions
    float2 TAAJitterUV;
    float2 PreviousTAAJitterUV;
    
    float3    PreviousCameraDirection;
    // Maximum number of basis to allocate to probes
    uint      MaxBasisCount;

    // Normally this is the same as FrameIndex, used for random number generation
    uint FrameSeed; 
    uint PreviousFrameSeed;

    // Screen resolution
    int2   ScreenDimensions;//
    float2 ScreenDimensionsInv;
    // Screen resolution, counted in tiles
    int2   TileDimensions;//
    float2 TileDimensionsInv;
    // Number of tiles / uniform screen probes
    int    UniformScreenProbeCount;

    // Budget for update rays
    int    UpdateRayBudget; //

    // SSRC parameters
    // Maximum number of adaptive probes to allocate
    int MaxAdaptiveProbeCount;

    // Misc parameters
    uint NoImportanceSampling;
    uint NoAdaptiveProbes;
    uint ResetCache;//

    // Learing parameters
    float CacheUpdateLearningRate;
    uint  CacheUpdate_SGColor;
    uint  CacheUpdate_SGDirection;
    uint  CacheUpdate_SGLambda;//

    uint  DebugVisualizeMode;
    uint  DebugVisualizeChannel;
    uint  DebugVisualizeIncidentRadianceNumPoints;

    float DebugTonemapExposure;//
    uint2 DebugCursorPixelCoords;

    // Used for single virtual emitter debugging
    uint   DebugLight;
    uint   UseAmbientOcclusion;//
    float3 DebugLightPosition;
    float  DebugLightSize;//
    float3 DebugLightColor;
    uint   UseNearFieldGI;
    uint   NoDenoiser;
    uint   DisableSG; //

    uint   BaseUpdateRayWaves;
    uint   ProbeFiltering;
    uint   SquaredSGDirectionalRadianceWeight;
};


// The screen tile size (in pixels) 
#define SSRC_TILE_SIZE 16
#define SSRC_TILE_SIZE_L2 4
#ifdef __cplusplus
static_assert((1 << SSRC_TILE_SIZE_L2) == SSRC_TILE_SIZE, "SSRC_TILE_SIZE != 1<<SSRC_TILE_SIZE_L2.");
#endif
#if SSRC_TILE_SIZE != (1 << SSRC_TILE_SIZE_L2)
#error "SSRC_TILE_SIZE != 1<<SSRC_TILE_SIZE_L2."
#endif

#define SSRC_MAX_NUM_BASIS_PER_PROBE 8
#define SSRC_MAX_NUM_UPDATE_RAY_PER_PROBE (32 * 8)

#define SSRC_MAX_ADAPTIVE_PROBE_LAYERS 2
#if (1<<SSRC_MAX_ADAPTIVE_PROBE_LAYERS) > SSRC_TILE_SIZE 
#error "1 << SSRC_MAX_ADAPTIVE_PROBE_LAYERS > SSRC_TILE_SIZE."
#endif

// The size of probe textures (hemispherical)
#define SSRC_PROBE_TEXTURE_SIZE 8
#define SSRC_PROBE_TEXTURE_TEXEL_COUNT (SSRC_PROBE_TEXTURE_SIZE * SSRC_PROBE_TEXTURE_SIZE)
#define SSRC_PROBE_TEXTURE_TEXEL_COUNT_L2 6
#if SSRC_PROBE_TEXTURE_TEXEL_COUNT != (1 << SSRC_PROBE_TEXTURE_TEXEL_COUNT_L2)
#error "SSRC_PROBE_TEXTURE_TEXEL_COUNT != 1<<SSRC_PROBE_TEXTURE_TEXEL_COUNT_L2."
#endif

#define MIGI_QUANTILIZE_RADIANCE_MULTIPLIER (4096.f)

#define SSRC_PROBE_NORMAL_OFFSET (1e-4f)

#ifdef __cplusplus
}// namespace Capsaicin
#endif
#endif // MIGI_COMMON_HLSL