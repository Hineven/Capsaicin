#ifndef MIGI_SHARED_PARAMETERS_HLSL
#define MIGI_SHARED_PARAMETERS_HLSL

// Shared structs within RAM & VRAM
#include "../../gpu_shared.h"
#include "migi_common.hlsl"

// GI1.0 invalid flag (to make the copy-pasted code from GI1.0 work)
#define kGI10_InvalidId 0xFFFFFFFFu

// Use heuristic for direction update instead of GD
#define HEURISTIC_DIRECTION_UPDATE
// LSM (least square error) for probe update,
// otherwise LAD (least absolute deviation), which reduces bias but needs more iterations to converge
#define OPTIMAL_COLOR_UPDATE
// Use LAD when doing SG reprojection guessing the intensity
// #define REPROJECTION_LAD
// Do not guess a new intensity for SGs in reprojection. Use their original values
// instead.
#define REPROJECTION_NO_INTENSITY_GUESSING
// Do not take probe sample weight into consideration when performing SG picking
// while doing reprojection.
// #define REPROJECTION_NO_SAMPLE_WEIGHT

// Whether to delay SG reprojection (history reuse) for better temporal stability
#define DELAYED_SG_REPROJECTION

// Evaluate backup radiance on probe texels instead of update rays
// Though this heuristic is a bit strange, it mitigates the issue of
// large backup values caused by the lack of update rays and importance sampling
// #define BACKUP_RADIANCE_ON_PROBE_TEXELS

// Fully stochastic SG selection
// #define PURE_STOCHASTIC_SG_SELECTION

// Do not considerate octahedron radiance when selecting SGs for reprojection
#define REPROJECTION_PRIORITIZE_SG

#define MIN_SG_LAMBDA 12.f
#define MAX_SG_LAMBDA 4000.f

// Rays that have raw evaluated SG values less than this won't be taken into account
// for the SG during update.
#define SG_CLIP_VALUE 1e-3f

// Merge SG Lambdas in log scale instead of linear
#define LOGSCALE_SG_LAMBDA_IN_MERGING

// Clamp negative radiance values when coordinating SG and Oct
// Gives a slightly more stable lighting but may introduce bias?
// #define CLAMP_NEGATIVE_RADIANCE_VALUES

// Use UE style hemispherical octahedron mapping
// It's not area preserving, and correction is not done.
// TODO there're darkening bugs in the current implementation.
// #define UE_STYLE_HEMISPHERICAL_OCT_MAPPING

// If we're using a more accurate (but more expensive)
// integration approximation for SG final shading.
// #define HIGH_PRECISION_SG_INTEGRATION

// Mirror repeat the tile jitter sequence.
// This helps elevating regular-patterned artifacts caused by
// biased SG reprojection
// #define MIRROR_REPEAT_TILE_JITTER_SEQUENCE

// Preserve irradiance by adjusting SG color when updating lambda
// Helps mitigate noise when lambda is unstable
// 1001: However, this will introduce unlimited growth in SG color under certain conditions.
// Massive degrading the quality of the result. Need a better way to ensure stability.
// #define SG_LAMBDA_UPDATE_PRESERVE_IRRADIANCE

// Always accumulate gradients of SGs when it evaluates above the target value
// Darkens? the scene but may helps with fireflies
// #define ALWAYS_ACCUMULATE_SG_GRADIENTS_IF_IT_IS_ABOVE_TARGET_VALUE

// Favor sharp and bright SGs pointing at potentially small and bright light sources
// to prevent losing them when reprojecting and bring artifacts.
// #define SG_REPPROJECTION_FAVOR_SHARP_BRIGHT_ONES

// Do not consider probe sample weights when doing SG reprojection
// #define REPROJECTION_NO_PROBE_SAMPLE_WEIGHT

// Jitter the probe position for stochastic reprojection (2x2 -> 3x3)
#define SCREEN_PROBE_TINY_STOCHASTIC_JITTER

// Debug flag to minimize the impact of SG blending & merging due to probe reprojection
#define DEBUG_MIN_PROBE_REPROJECTION

#ifndef WAVE_SIZE
// This macro should be set correctly with the compiler flags
#error "WAVE_SIZE is not set! Shaders will malfunction."
// NVIDIA: 32, AMD: 64
#endif

// Descripotor contents

TextureCube g_EnvironmentBuffer;
Texture2D g_TextureMaps[] : register(space99);
SamplerState g_NearestSampler;
SamplerState g_TextureSampler; // Is a linear sampler and set to repeat wrapping.
SamplerState g_LinearSampler;  // Clamp to edge
SamplerState g_ClampedPointSampler; // Clamp to edge, point sampling

// Common buffers of GPU scene

StructuredBuffer<uint>     g_IndexBuffer;
StructuredBuffer<Vertex>   g_VertexBuffer;

StructuredBuffer<Mesh>     g_MeshBuffer;
StructuredBuffer<Instance> g_InstanceBuffer;
StructuredBuffer<Material> g_MaterialBuffer;
StructuredBuffer<float3x4> g_TransformBuffer;

RaytracingAccelerationStructure g_Scene;

// G-Buffers & History
Texture2D g_DepthTexture;
Texture2D g_VisibilityTexture;
Texture2D g_GeometryNormalTexture;
Texture2D g_ShadingNormalTexture;
Texture2D g_VelocityTexture;

Texture2D g_PreviousDepthTexture;
Texture2D g_PreviousGeometryNormalTexture;
Texture2D g_PreviousShadingNormalTexture;
Texture2D g_PrevCombinedIlluminationTexture;

Texture2D g_BentNormalAndOcclusionTexture;
Texture2D g_NearFieldGlobalIlluminationTexture;


// Buffers for indirect draw / dispatch command generation (See kernel GenerateDispatch() / GenerateDraw())
uint g_GroupSize;
StructuredBuffer<uint> g_CountBuffer;
RWStructuredBuffer<DispatchCommand>     g_RWDispatchCommandBuffer;
RWStructuredBuffer<DispatchCommand>     g_RWPerLaneDispatchCommandBuffer;
RWStructuredBuffer<DispatchCommand>     g_RWWCSDispatchCommandBuffer; // World cache spawn request
RWStructuredBuffer<DispatchCommand>     g_RWPerLaneWCSDispatchCommandBuffer;
RWStructuredBuffer<DispatchCommand>     g_RWWCAPDispatchCommandBuffer; // World cache active probe
RWStructuredBuffer<DispatchCommand>     g_RWPerLaneWCAPDispatchCommandBuffer;
RWStructuredBuffer<DispatchRaysCommand> g_RWDispatchRaysCommandBuffer;
RWStructuredBuffer<DrawCommand>         g_RWDrawCommandBuffer;
RWStructuredBuffer<DrawIndexedCommand>  g_RWDrawIndexedCommandBuffer;
RWStructuredBuffer<uint>                g_RWReduceCountBuffer;

// Outputs
RWTexture2D<float4> g_RWDebugOutput;
RWTexture2D<float4> g_RWGlobalIlluminationOutput;

// Buffers
// Sparse screen space cache
// Probe headers
// Use textures for better texture cache utilization (2x2)
// BasisOffset : 24 bits
// ProbeClass  : 4  bits
// ProbeFlag   : 4  bits
RWTexture2D<uint>   g_RWProbeHeaderPackedTexture;
RWTexture2D<uint>   g_RWProbeScreenCoordsTexture;
RWTexture2D<float>  g_RWProbeLinearDepthTexture;
RWTexture2D<float4> g_RWProbeWorldPositionTexture; 
RWTexture2D<uint>   g_RWProbeNormalTexture;
RWTexture2D<uint>   g_RWPreviousProbeHeaderPackedTexture;
RWTexture2D<uint>   g_RWPreviousProbeScreenCoordsTexture;
RWTexture2D<float>  g_RWPreviousProbeLinearDepthTexture;
RWTexture2D<float4> g_RWPreviousProbeWorldPositionTexture;
RWTexture2D<uint>   g_RWPreviousProbeNormalTexture;
// Probe color maps
// R16G16B16A16, 8x8 per probe, 3 color + 1 linear depth
RWTexture2D<float4>  g_RWProbeColorTexture; 
RWTexture2D<float4>  g_RWPreviousProbeColorTexture;
// Padded probe color texture for hardware filtering
RWTexture2D<float4>  g_RWProbeSampleColorTexture;
Texture2D<float4>    g_ProbeSampleColorTexture;
// SH coefficients 8+1 per channel
RWTexture2D<float4>  g_RWProbeSHCoefficientsRTexture;
RWTexture2D<float4>  g_RWProbeSHCoefficientsGTexture;
RWTexture2D<float4>  g_RWProbeSHCoefficientsBTexture;
RWTexture2D<float4>  g_RWProbeIrradianceTexture;
// The SG storage for SSRC probes
// Color : 16*3, Lambda: 16, Normal: 32packed, Linear Depth: 32
RWStructuredBuffer<uint>   g_RWProbeSGBuffer;
RWStructuredBuffer<uint>   g_RWPreviousProbeSGBuffer;
// Used when allocating SGs to probes
RWStructuredBuffer<uint>   g_RWAllocatedProbeSGCountBuffer;

// The estimated accuracy of the current probe from temporal reprojection
// [0, 1], used to guide update ratio
RWTexture2D<float>  g_RWProbeHistoryTrustTexture;

// Number of update rays allocated for each probe
// Must be a multiple of WAVE_SIZE
RWStructuredBuffer<uint>  g_RWProbeUpdateRayCountBuffer;
RWStructuredBuffer<uint>  g_RWProbeUpdateRayOffsetBuffer;
// Total number of allocated update rays
RWStructuredBuffer<uint>  g_RWUpdateRayCountBuffer;
// Index probe index with ray index, screen probe: unorm16x2, world probe: index with 0x80000000u
RWStructuredBuffer<uint>  g_RWUpdateRayProbeBuffer;
// Octahedral packed direction for each update ray (fp16x2)
RWStructuredBuffer<uint>  g_RWUpdateRayDirectionBuffer;
// Traced Radiance & InvPdf for each update ray
RWStructuredBuffer<uint2>  g_RWUpdateRayRadianceInvPdfBuffer;
RWStructuredBuffer<float>  g_RWUpdateRayLinearDepthBuffer;

// Number of adaptive probes within each tile
RWTexture2D<uint>          g_RWTileAdaptiveProbeCountTexture;
RWTexture2D<uint>          g_RWPreviousTileAdaptiveProbeCountTexture;
RWTexture2D<uint>          g_RWNextTileAdaptiveProbeCountTexture;
// Adaptive probe indices for each tile. The indexing rules are the same as Lumen.
RWTexture2D<uint>          g_RWTileAdaptiveProbeIndexTexture;
RWTexture2D<uint>          g_RWPreviousTileAdaptiveProbeIndexTexture;
// Count of adaptive probes allocated for this frame.
RWStructuredBuffer<uint>   g_RWAdaptiveProbeCountBuffer;
// Error for probe updates. Used to splat onto the screen error texture to guide ray allocation.
RWStructuredBuffer<float>  g_RWProbeUpdateErrorBuffer;

// The splatted error texture for probes. Used to allocate update rays for probes.
RWTexture2D<float>  g_RWUpdateErrorSplatTexture;
Texture2D<float>    g_UpdateErrorSplatTexture;
// Used for reprojection
Texture2D<float>    g_PreviousUpdateErrorSplatTexture;

// GI Denoising
RWTexture2D<float4>  g_RWIrradianceTexture;
Texture2D<float4>  g_PreviousIrradianceTexture;
RWTexture2D<float4>  g_RWGlossySpecularTexture;
Texture2D<float4>  g_PreviousGlossySpecularTexture;
Texture2D<unorm float>     g_PreviousHistoryAccumulationTexture;
RWTexture2D<unorm float>   g_RWHistoryAccumulationTexture;
// Global illumination history without skybox
Texture2D<float4>          g_PreviousGlobalIlluminationTexture;
// Diffuse only render result
RWTexture2D<float4>          g_RWDiffuseGITexture;
Texture2D<float4>         g_PreviousDiffuseGITexture;

// HiZ buffer generation input-outputs
RWTexture2D<float4> g_RWHiZ_In;
RWTexture2D<float4> g_RWHiZ_Out;

Texture2D g_TileHiZ_Min;
Texture2D g_TileHiZ_Max;

// Constant buffers for sub-components
ConstantBuffer<WorldCacheConstants> WorldCache;
ConstantBuffer<RTConstants>         RayTracing;
ConstantBuffer<MIGI_Constants>      MI;

// LUT
Texture2D g_LutBuffer;
uint g_LutSize;

// UE style hemi octahedron mapping correction lut
// Stores the area percentage of each pixel occupied on the hemisphere
Texture2D g_UEHemiOctahedronCorrectionLutTexture;
RWTexture2D<float> g_RWUEHemiOctahedronCorrectionLutTexture;
RWStructuredBuffer<float> g_RWUEHemiOctahedronCorrectionLutTempBuffer;

// Buffer for exporting data to CPU
RWStructuredBuffer<uint> g_RWExportBuffer;

// Buffers for visualizing probes
StructuredBuffer<float3> g_IcoSphereVertexBuffer;
RWStructuredBuffer<float3> g_RWVisVPVNBuffer;
StructuredBuffer<float3> g_VisVPVNBuffer;

// Debugging
RWStructuredBuffer<float3> g_RWDebugCursorWorldPosBuffer;
RWStructuredBuffer<float3> g_RWDebugProbeWorldPositionBuffer;
RWStructuredBuffer<float3> g_RWDebugVisualizeIncidentRadianceBuffer;
RWStructuredBuffer<int2>   g_RWDebugProbeIndexBuffer;

#endif // MIGI_SHARED_PARAMETERS_HLSL