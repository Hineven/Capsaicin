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

float4x4 g_Reprojection;

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
RWStructuredBuffer<uint4>               g_RWDrawCommandBuffer;

// Outputs & RW Buffers
RWTexture2D<float4> g_RWDebugOutput;
RWTexture2D<float4> g_RWGlobalIlluminationOutput;

// Parameters
uint g_NoImportanceSampling;

RWTexture2D<float4> g_RWBasisParameterTexture;
RWTexture2D<float4> g_RWBasisColorTexture;
RWTexture2D<float4> g_RWBasisParameterGradientTexture;
RWTexture2D<float4> g_RWBasisColorGradientTexture;
RWTexture2D<float4> g_RWRadianceXTexture;
RWTexture2D<float4> g_RWRadianceYTexture;

Texture2D<float4> g_RadianceXTexture;
Texture2D<float4> g_RadianceYTexture;

RWTexture2D<float4> g_RWRayDirectionTexture;
RWTexture2D<float4> g_RWRayRadianceTexture;
// RayRadiance - CacheEvaluatedRadiance
RWTexture2D<float4> g_RWRayRadianceDifferenceTexture;

int2 g_ScreenCacheDimensions;
float g_CacheUpdateLearningRate;

// Resolution
int2 g_OutputDimensions;

// Constant buffers for sub-components
ConstantBuffer<HashGridCacheConstants>     g_HashGridCacheConstants;
ConstantBuffer<WorldSpaceReSTIRConstants>  g_WorldSpaceReSTIRConstants;
ConstantBuffer<RTConstants>  g_RTConstants;


#endif // MIGI_SHARED_PARAMETERS_HLSL