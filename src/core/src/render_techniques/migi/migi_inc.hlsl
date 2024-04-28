#ifndef MIGI_SHARED_PARAMETERS_HLSL
#define MIGI_SHARED_PARAMETERS_HLSL

// Common structs within RAM & VRAM
#include "../../gpu_shared.h"
#include "migi_common.hlsl"

// GI1.0 invalid flag (to make the copy-pasted code from GI1.0 work)
#define kGI10_InvalidId 0xFFFFFFFFu

TextureCube g_EnvironmentBuffer;
Texture2D g_TextureMaps[] : register(space99);
SamplerState g_NearestSampler;
SamplerState g_TextureSampler; // Is a linear sampler.
SamplerState g_LinearSampler;

// Common buffers of GPU scene


StructuredBuffer<uint>     g_IndexBuffer;
StructuredBuffer<Vertex>   g_VertexBuffer;

StructuredBuffer<Mesh>     g_MeshBuffer;
StructuredBuffer<Instance> g_InstanceBuffer;
StructuredBuffer<Material> g_MaterialBuffer;
StructuredBuffer<float3x4> g_TransformBuffer;

RaytracingAccelerationStructure g_Scene;

// Common view parameters
float3 g_CameraPosition;
float3 g_CameraDirection;
float g_CameraFoVY;
float g_CameraFoVY2;
float g_AspectRatio;
float g_CameraNear;
float g_CameraFar;
float3 g_CameraUp;
float3 g_CameraRight;
float4x4 g_CameraView;
float4x4 g_CameraProjView;
float4x4 g_CameraViewInv;
float4x4 g_CameraProjViewInv;
// The scale of a single pixel in the standard camera plane (z = 1)
float g_CameraPixelScale;

// Camera space : Current -> Prev
float4x4 g_Reprojection;
// Camera space : Prev -> Current
float4x4 g_ForwardProjection;

float3 g_PreviousCameraPosition;

uint g_FrameIndex;

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
RWStructuredBuffer<uint>   g_RWActiveBasisCountBuffer;
RWStructuredBuffer<uint>   g_RWActiveBasisIndexBuffer;
// Cache per instance effective radius for injection rasterization
RWStructuredBuffer<float>  g_RWBasisEffectiveRadiusBuffer;
// Film space effective radius for clipping in injection
RWStructuredBuffer<float>  g_RWBasisEffectiveRadiusFilmBuffer;
// Cached basis center projection on screen, unorm 2x16 packed
// RWStructuredBuffer<uint>   g_RWBasisFilmPositionBuffer;
RWStructuredBuffer<float3> g_RWBasisLocationBuffer;
// Color : 16*3, Lambda: 16, Normal: 32packed, WLambda: 16, WAlpha: 16
RWStructuredBuffer<uint>   g_RWBasisParameterBuffer; // Data storage. 10 Numbers packed in 16 bytes.
// Color, Lambda, Normal, WLambda, WAlpha (9)
RWStructuredBuffer< int>   g_RWQuantilizedBasisStepBuffer; // Step size for atomic accumulation
RWStructuredBuffer<float>  g_RWUpdateStepScaleSumsBuffer; // Sums of the step scale among waves.
RWStructuredBuffer<float>  g_RWUpdateStepScaleBuffer;
RWStructuredBuffer<uint>   g_RWBasisFlagsBuffer; // Flag bits for basis
RWStructuredBuffer<uint>   g_RWFreeBasisIndicesBuffer; // The free indices of the basis.
RWStructuredBuffer<uint>   g_RWFreeBasisIndicesCountBuffer;
RWStructuredBuffer<uint>   g_RWTileBasisCountBuffer; // The number of injected basis in each tile
// RWStructuredBuffer<uint>   g_RWTileBasisCountOldBuffer; // Mark the number of injected basis for each tile before basis generation
RWStructuredBuffer<uint>   g_RWTileRayCountBuffer; // The number of update rays allocated per tile
RWStructuredBuffer<uint>   g_RWTileRayOffsetBuffer; // Offset of update ray index for each tile
RWStructuredBuffer<uint>   g_RWUpdateRayDirectionBuffer; // Sampled update ray direction
RWStructuredBuffer<uint>   g_RWUpdateRayOriginBuffer;    // int16x2 packed
// Pdf: tile pixel pdf (normalized by tile pixel count) * ray direction pdf
RWStructuredBuffer<uint2>  g_RWUpdateRayRadiancePdfBuffer; // Sampled update ray radiance and directional pdf.
RWStructuredBuffer<uint2>  g_RWUpdateRayCacheBuffer; // Cache evaluated radiance + wsum, fp16x4 packed
RWStructuredBuffer<uint>   g_RWUpdateRayCountBuffer; // Count of update rays generated for this frame.
RWStructuredBuffer<float>  g_RWTileUpdateErrorSumsBuffer; // Sums of update error sums of WAVE_SIZE tiles. Used for precompuation for ray allocation.
RWStructuredBuffer<float>  g_RWUpdateErrorBuffer; // Sum of all tile update errors.
// Before compression
// TILE_BASIS_INJECTION_RESERVATION slots are reserved for each tile for injection.
RWStructuredBuffer<uint>   g_RWTileBasisIndexInjectionBuffer;
// Compressed
RWStructuredBuffer<uint>   g_RWTileBaseSlotOffsetBuffer;  // Points to the first basis indice index
RWStructuredBuffer<uint>   g_RWTileBasisIndexBuffer; // Store indices
int2   g_TileDimensions; // Number of tiles in x and y direction
float2 g_TileDimensionsInv;
float  g_BasisWInitialRadius; // Initial radius of each basis's W in pixels
float  g_MinWeightE; // A super parameter for determining the basis effective radius.
float  g_BasisSpawnCoverageThreshold; // The threshold for spawning new basis.
int    g_MaxBasisCount; // Size of the basis buffer
int    g_UpdateRayBudget; // Budget for update rays
float  g_WCoveragePadding; // Padding for W when computing coverage
float  g_TileFractionPadding; // Pad the tile fraction for ray allocation, 0: fully importance, 1: avg.

// Conservative Rasterization for index injection
int g_CR_DiskVertexCount; // Number of vertices in the disk when injecting basis
float g_CR_DiskRadiusMultiplier; // Multiplier for the disk radius
float g_CR_DiskRadiusBias; // Bias for the disk radius

// Misc parameters
uint g_NoImportanceSampling;
uint g_FixedStepSize;
uint g_FreezeBasisAllocation;

// Coverage texture supplies the cache generation, currently unused
RWTexture2D<float4> g_RWCacheCoverageTexture;
// The splatted error texture for update rays
// Used for importance sampling update ray pixel positions
RWTexture2D<float>  g_RWUpdateErrorSplatTexture;
Texture2D<float>  g_UpdateErrorSplatTexture;
// Used for reprojection
Texture2D<float>    g_PreviousUpdateErrorSplatTexture;

// HiZ buffer generation input-outputs
RWTexture2D<float4> g_RWHiZ_In;
RWTexture2D<float4> g_RWHiZ_Out;

Texture2D g_TileHiZ_Min;
Texture2D g_TileHiZ_Max;

float g_CacheUpdateLearningRate;
uint  g_CacheUpdate_SGColor;
uint  g_CacheUpdate_SGDirection;
uint  g_CacheUpdate_SGLambda;
uint  g_CacheUpdate_WAlpha;
uint  g_CacheUpdate_WLambda;

// Screen resolution
int2   g_OutputDimensions;
float2 g_OutputDimensionsInv;

// Constant buffers for sub-components
ConstantBuffer<HashGridCacheConstants>     g_HashGridCacheConstants;
ConstantBuffer<WorldSpaceReSTIRConstants>  g_WorldSpaceReSTIRConstants;
ConstantBuffer<RTConstants>  g_RTConstants;

uint g_DebugVisualizeMode;
uint g_DebugVisualizeChannel;

float g_DebugTonemapExposure;

#endif // MIGI_SHARED_PARAMETERS_HLSL