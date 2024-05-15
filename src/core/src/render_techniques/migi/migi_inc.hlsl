#ifndef MIGI_SHARED_PARAMETERS_HLSL
#define MIGI_SHARED_PARAMETERS_HLSL

// Common structs within RAM & VRAM
#include "../../gpu_shared.h"
#include "migi_common.hlsl"

// GI1.0 invalid flag (to make the copy-pasted code from GI1.0 work)
#define kGI10_InvalidId 0xFFFFFFFFu

// Flags set with compiler flags

// Use heuristic for direction update
// #define HEURISTIC_DIRECTION_UPDATE
// Use RMSE to guide update ray allocation
// #define ERROR_RMSE

#ifndef WAVE_SIZE
// This macro is set with the compiler flags
// Default to NVIDIA
#define WAVE_SIZE 32
#endif

// Descripotor contents

TextureCube g_EnvironmentBuffer;
Texture2D g_TextureMaps[] : register(space99);
SamplerState g_NearestSampler;
SamplerState g_TextureSampler; // Is a linear sampler and set to repeat wrapping.
SamplerState g_LinearSampler;  // Clamp to edge

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


// Buffers for indirect draw / dispatch command generation (See kernel GenerateDispatch() / GenerateDraw())
uint g_GroupSize;
StructuredBuffer<uint> g_CountBuffer;
RWStructuredBuffer<DispatchCommand>     g_RWDispatchCommandBuffer;
RWStructuredBuffer<DispatchRaysCommand> g_RWDispatchRaysCommandBuffer;
RWStructuredBuffer<DrawCommand>         g_RWDrawCommandBuffer;
RWStructuredBuffer<DrawIndexedCommand>  g_RWDrawIndexedCommandBuffer;
RWStructuredBuffer<uint>                g_RWReduceCountBuffer;

// Outputs
RWTexture2D<float4> g_RWDebugOutput;
RWTexture2D<float4> g_RWGlobalIlluminationOutput;

// Buffers
// Sparse screen space cache
// The count of overall allocated probes
RWStructuredBuffer<uint>   g_RWActiveProbeCountBuffer;
// Probe headers
// Use textures for better texture cache utilization (2x2)
// BasisOffset : 24 bits
// ProbeRank   : 4  bits
// ProbeFlag   : 4  bits
RWTexture2D<uint>   g_RWProbeHeaderPackedTexture;
RWTexture2D<uint>   g_RWProbeScreenPositionTexture;
RWTexture2D<float>  g_RWProbeLinearDepthTexture;
RWTexture2D<float3> g_RWProbeWorldPositionTexture; 
RWTexture2D<unorm float2> g_RWProbeNormalTexture;
RWTexture2D<uint>   g_RWPreviousProbeHeaderPackedTexture;
RWTexture2D<uint>   g_RWPreviousProbeScreenPositionTexture;
RWTexture2D<float>  g_RWPreviousProbeLinearDepthTexture;
RWTexture2D<float3> g_RWPreviousProbePositionTexture;
RWTexture2D<unorm float2> g_RWPreviousProbeNormalTexture; 
// The SG storage for SSRC probes
// Color : 16*3, Lambda: 16, Normal: 32packed, Linear Depth: 32
RWStructuredBuffer<uint>   g_RWProbeSGBuffer;
RWStructuredBuffer<uint>   g_RWPreviousProbeSGBuffer;
// Used when allocating SGs to probes
RWStructuredBuffer<uint>   g_RWAllocatedProbeSGCountBuffer;
// Irradiance for SSRC probes
// Color : 16*3, Unused: 16
RWStructuredBuffer<uint>  g_RWProbeIrradianceBuffer;
RWStructuredBuffer<uint>  g_RWPreviousProbeIrradianceBuffer;
// Exponential moving average of gradient squares (color, lambda, direction), 16*2, 32
RWStructuredBuffer<uint2>  g_RWProbeSGGradientScaleBuffer;
RWStructuredBuffer<uint2>  g_RWPreviousProbeSGGradientScaleBuffer;  

// Number of update rays allocated for each probe
// Must be a multiple of WAVE_SIZE
RWStructuredBuffer<uint>  g_RWTileUpdateRayCountBuffer;

// Sampling, tracing and updating are done in a single kernel, so we do not need to store update rays

// Number of adaptive probes within each tile
RWTexture2D<uint>          g_RWTileAdaptiveProbeCountTexture;
RWTexture2D<uint>          g_RWPreviousTileAdaptiveProbeCountTexture;
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

// HiZ buffer generation input-outputs
RWTexture2D<float4> g_RWHiZ_In;
RWTexture2D<float4> g_RWHiZ_Out;

Texture2D g_TileHiZ_Min;
Texture2D g_TileHiZ_Max;

// Constant buffers for sub-components
ConstantBuffer<HashGridCacheConstants>     g_HashGridCacheConstants;
ConstantBuffer<WorldSpaceReSTIRConstants>  g_WorldSpaceReSTIRConstants;
ConstantBuffer<RTConstants>  g_RTConstants;

ConstantBuffer<MIGI_Constants>              MI;

// Debugging
RWStructuredBuffer<float3> g_RWDebugCursorWorldPosBuffer;
RWStructuredBuffer<float3> g_RWDebugVisualizeIncidentRadianceBuffer;


// Varying parameters for multiple invocations of the same kernel
int g_AdaptiveProbeDownsampleFactor;

#endif // MIGI_SHARED_PARAMETERS_HLSL