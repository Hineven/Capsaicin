#ifndef MIGI_SHARED_HLSL
#define MIGI_SHARED_HLSL

#include "migi_inc.hlsl"

#include "../../materials/material_sampling.hlsl"
#include "../../components/light_sampler_grid_stream/light_sampler_grid_stream.hlsl"
#include "../../components/stratified_sampler/stratified_sampler.hlsl"
#include "../../components/blue_noise_sampler/blue_noise_sampler.hlsl"

#include "../../math/math_constants.hlsl"
#include "../../math/sampling.hlsl"
#include "../../math/transform.hlsl"
#include "../../math/hash.hlsl"
#include "../../math/pack.hlsl"

#include "../../geometry/mesh.hlsl"
#include "../../geometry/intersection.hlsl"

// Project a point in screen space to world space
// Transform: InvViewProj
float3 InverseProject(in float4x4 transform, in float2 uv, in float depth)
{
    float4 homogeneous = mul(transform, float4(2.0f * float2(uv.x, 1.0f - uv.y) - 1.0f, depth, 1.0f));
    return homogeneous.xyz / homogeneous.w; // perspective divide
}

// The ray reaches out to the camera near plane
float3 GetCameraRayDirectionUnnormalized (float2 NDC) {
    float Scale = tan(g_CameraFoVY * 0.5f);
    float3 Right = g_CameraRight * Scale * g_AspectRatio;
    float3 Up = g_CameraUp * Scale;
    return NDC.x * Right + NDC.y * Up + g_CameraDirection;
}

float3 GetCameraRayDirection (float2 NDC) {
    return normalize(GetCameraRayDirectionUnnormalized(NDC));
}

/**
 * Determine a transformation matrix to correctly transform normal vectors.
 * @param transform The original transform matrix.
 * @return The new transform matrix.
 */
float3x3 GetNormalTransform(float4x4 transform)
{
    // The transform for a normal is transpose(inverse(M))
    // The inverse is calculated as [1/det(A)]*transpose(C) where C is the cofactor matrix
    // This simplifies down to [1/det(A)]*C
    float3x3 input = (float3x3)transform;
    float3x3 result;
    result._m00 = determinant(float2x2(input._m11_m12, input._m21_m22));
    result._m01 = -determinant(float2x2(input._m10_m12, input._m20_m22));
    result._m02 = determinant(float2x2(input._m10_m11, input._m20_m21));
    result._m10 = -determinant(float2x2(input._m01_m02, input._m21_m22));
    result._m11 = determinant(float2x2(input._m00_m02, input._m20_m22));
    result._m12 = -determinant(float2x2(input._m00_m01, input._m20_m21));
    result._m20 = determinant(float2x2(input._m01_m02, input._m11_m12));
    result._m21 = -determinant(float2x2(input._m00_m02, input._m10_m12));
    result._m22 = determinant(float2x2(input._m00_m01, input._m10_m11));
#undef minor
    float3 det3 = input._m00_m01_m02 * result._m00_m01_m02;
    float det = det3.x + det3.y + det3.z;
    det = 1.0f / det;
    return (result * det);
}

//******************************************
//              Basis Helpers
//******************************************

struct SGData {
    float3 Direction;
    float Lambda;
    float3 Color;
};

float OneSubExpNeg2Lambda (float lambda) {
    return lambda < 24.f ? (1.f - exp(-2.f * lambda)) : 1.f;
}

float SGNormalizationFactor (float lambda) {
    return lambda / (TWO_PI * OneSubExpNeg2Lambda(lambda));
}

float3 EvaluateSG(SGData SG, float3 Direction)
{
    return SG.Color * exp(SG.Lambda * dot(SG.Direction, Direction) - 1.f);
}

float EvaluateNormalizedSG (SGData SG, float3 Direction) {
    float raw = exp(SG.Lambda * (dot(SG.Direction, Direction) - 1.f));
    // Normalize the SG to make its integral equals to 1
    return raw * SGNormalizationFactor(SG.Lambda);
}

struct WData {
    float Lambda;
    float Alpha;
};

float EvaluateW (WData WD, float3 Delta)
{
    float LinearDistance = length(Delta);
    return max(WD.Alpha - WD.Lambda * LinearDistance, 0);
}

struct WGradients {
    float dLambda;
    float dAlpha;
};

void EvaluateW_Gradients (WData WD, float3 Delta, out WGradients Gradients)
{
    float LinearDistance = length(Delta);
    bool Within = (LinearDistance * WD.Lambda) <= WD.Alpha;
    Gradients.dLambda = Within ? -LinearDistance : 0;
    Gradients.dAlpha  = Within ? -1 : 0;
}

float EvaluateWEffectiveRadius (WData WD) {
    return max(1.f / WD.Lambda, 0);
}

WData ShiftW (WData WD, float3 Delta) {
    float LinearDistance = length(Delta);
    WD.Alpha = max(WD.Alpha - LinearDistance * WD.Lambda, 0);
    return WD;
}

bool IsEmptyW (WData WD) {
    return WD.Alpha <= 0.f || WD.Lambda <= 0.001f;
}

float EvaluateBilateralFilterWeight (float PixelScale, float FilmPlaneRadius, float3 DeltaPosition, float3 ShadingPixelNormal, float3 LightingPixelNormal) {
    // Bilaterally filter the neighbor reservoir
    float DirectionalDecay = max(dot(ShadingPixelNormal, LightingPixelNormal), 0.0f);
    DirectionalDecay = squared(squared(DirectionalDecay)); // make it steep
    //return DirectionalDecay;
    // return DirectionalDecay;
    float Distance = length(DeltaPosition);
    float Radius = 4.f * PixelScale * FilmPlaneRadius;
    float DistanceDecay = exp(- Distance / Radius);
    return DirectionalDecay * DistanceDecay;
}

struct SGGradients {
    float dLambda;
    float3 dColor;
    float3 dDirection;
};

// error function: (x - y) ^ 2
// void EvaluateSG_Gradients (SGData SG, float3 TargetDirection, float3 TargetRadiance, float3 CurrentRadiance, out SGGradients Gradients) {
//     // Compute the Gradients for SG parameters
//     // Targeting at the remaining radiance after subtracting all other SH and SGs
//     float W1 = (dot(SG.Direction, TargetDirection) - 1);
//     float3 W2 = exp(SG.Lambda * W1);
//     float3 W3 = 2 * W2 * (SG.Color * W2 - TargetRadiance);
//     // d (sg_theta(dir) - v)^2 / d theta
//     Gradients.dLambda = dot(SG.Color * W3 * (dot(SG.Direction, TargetDirection) - 1.f), float3(1, 1, 1));
//     Gradients.dColor = W3;
//     Gradients.dDirection = TargetDirection * dot(SG.Color * SG.Lambda * W3, float3(1, 1, 1));
// }

void EvaluateSG_Gradients (SGData SG, float3 TargetDirection, out SGGradients Gradients) {
    // Compute the Gradients for SG parameters
    // Targeting at the remaining radiance after subtracting all other SH and SGs
    float W1 = dot(SG.Direction, TargetDirection) - 1;
    float W2 = exp(SG.Lambda * W1);
    float W3 = W1 * W2;
    float SGColorScale = dot(SG.Color, float3(1, 1, 1));
    Gradients.dLambda    = W3 * SGColorScale;
    Gradients.dColor     = W2.xxx;
    Gradients.dDirection = TargetDirection * W2 * SGColorScale * SG.Lambda;
}

uint QuantilizeNormGradient (float V) {
    return uint(V * 1000000);
}
uint QuantilizeRadianceGradient (float Radiance) {
    return uint(Radiance * 100000);
}
uint QuantilizeLambdaGradient    (float dL) {
    return uint(dL * 1000000);
}

float SampleSGCosTheta (float u, float lambda) {
    return min(log(u + (1 - u) * exp(-2.f * lambda)) / lambda + 1, 1.f);
}

float SampleSGPDF(float lambda, float cosTheta) {
    return SGNormalizationFactor(lambda) * exp(lambda * (cosTheta - 1));
}

float3 SampleSG (float2 u, float lambda, out float pdf) {
    float cosTheta = SampleSGCosTheta(u.x, lambda);
    float sinTheta = sqrt(max(0.f, 1.f - cosTheta * cosTheta));
    float phi = TWO_PI * u.y;
    pdf = SampleSGPDF(lambda, cosTheta);
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float3 unpackSnorm10_10_12 (uint packed) {
    float3 ret;
    ret.x = (packed & 0x3FF) * (2.f / 1024.f) - (1.f - 1.f / 2048.f);
    ret.y = ((packed >> 10) & 0x3FF) * (2.f / 1024.f) - (1.f - 1.f / 2048.f);
    ret.z = ((packed >> 20) & 0xFFF) * (2.f / 4096.f) - (1.f - 1.f / 8192.f);
    return ret;
}

uint packSnorm10_10_12 (float3 normal) {
    uint x = uint(clamp(normal.x * 512.f + 512f, 0.f, 1023.f));
    uint y = uint(clamp(normal.y * 512.f + 512f, 0.f, 1023.f));
    uint z = uint(clamp(normal.z + 2048.f + 2048.f, 0.f, 4095.f));
    return x | (y << 10) | (z << 20);
}

uint4 FetchBasisData_W_Packed (int BasisIndex) {
    uint P0 = g_RWBasisParameterBuffer[BasisIndex * 4];
    uint P1 = g_RWBasisParameterBuffer[BasisIndex * 4 + 1];
    uint P2 = g_RWBasisParameterBuffer[BasisIndex * 4 + 2];
    uint P3 = g_RWBasisParameterBuffer[BasisIndex * 4 + 3];
    return uint4(P0, P1, P2, P3);
}
uint FetchBasisLocationPacked (int BasisIndex) {
    return g_RWBasisLocationBuffer[BasisIndex];
}

void UnpackBasisData (uint3 Packed, out SGData SG) {
    SG.Color     = float3(f16tof32(Packed.x & 0xFFFF), f16tof32(Packed.x >> 16), f16tof32(Packed.y & 0xFFFF));
    SG.Lambda    = f16tof32(Packed.y >> 16);
    SG.Direction = unpackSnorm10_10_12(Packed.z).xyz;
}

uint3 PackBasisData (SGData SG) {
    uint3 Packed;
    Packed.x = f32tof16(SG.Color.x) | (f32tof16(SG.Color.y) << 16);
    Packed.y = f32tof16(SG.Color.z) | (f32tof16(SG.Lambda) << 16);
    Packed.z = packSnorm10_10_12(SG.Direction);
    return Packed;
}

void WriteBasisData (int BasisIndex, SGData SG) {
    uint3 Packed = PackBasisData(SG);
    g_RWBasisParameterBuffer[BasisIndex * 4] = Packed.x;
    g_RWBasisParameterBuffer[BasisIndex * 4 + 1] = Packed.y;
    g_RWBasisParameterBuffer[BasisIndex * 4 + 2] = Packed.z;
}

void UnpackBasisLocation (uint Packed, out float2 UV) {
    UV.x = unpackUnorm1x16(Packed & 0xFFFF) * g_InvOutputDimensions.x;
    UV.y = unpackUnorm1x16(Packed >> 16) * g_InvOutputDimensions.y;
}

void FetchBasisLocation (int BasisIndex, out float2 UV) {
    uint Packed = FetchBasisLocationPacked(BasisIndex);
    UnpackBasisLocation(Packed, UV);
}

// Call it only after g_RWBasisCenterDepthBuffer is filled
float3 ReconstructBasisWorldPosition (int BasisIndex) {
    float2 UV;
    FetchBasisLocation(BasisIndex, UV);
    float Depth = g_RWBasisCenterDepthBuffer[BasisIndex].x;
    return InverseProject(g_CameraProjViewInv, UV, Depth);
}

bool IsBasisActive (int BasisIndex) {
    uint Flags = g_RWBasisFlagsBuffer[BasisIndex];
    return Flags != 0;
}

uint PackWData (WData WD) {
    return f32tof16(WD.Lambda) | (packUnorm1x16(WD.Alpha) << 16);
}

void UnpackWData (uint Packed, out WData WD) {
    WD.Lambda = f16tof32(Packed & 0xFFFF);
    WD.Alpha  = unpackUnorm1x16(Packed >> 16);
}

void FetchBasisData_W (int BasisIndex, out SGData SG, out WData WD) {
    uint4 Packed = FetchBasisData_W_Packed(BasisIndex);
    UnpackBasisData(Packed.xyz, SG);
    UnpackWData(Packed.w, WD);
}

void WriteBasisWData (int BasisIndex, WData WD) {
    g_RWBasisParameterBuffer[BasisIndex * 4 + 3] = PackWData(WD);
}

uint4 PackBasisData_W (SGData SG, WData WD) {
    uint3 PackedSG = PackBasisData(SG);
    uint PackedW = PackWData(WD);
    return uint4(PackedSG.x, PackedSG.y, PackedSG.z, PackedW);
}

void WriteBasisData_W (int BasisIndex, SGData SG, WData WD) {
    WriteBasisData(BasisIndex, SG);
    WriteBasisWData(BasisIndex, WD);
}

void WriteBasisLocation (int BasisIndex, float2 UV) {
    g_RWBasisLocationBuffer[BasisIndex] = 
        packUnorm1x16(UV.x * g_OutputDimensions.x) 
        + (packUnorm1x16(UV.y * g_OutputDimensions.y) << 16);
}

// Tile index related
void ScreenCache_InjectBasisIndexToTile (int2 TileCoords, int BasisIndex) {
    int TileIndex  = TileCoords.x + TileCoords.y * g_TileDimensions.x;
    int SlotBase   = TileIndex * TILE_BASIS_INJECTION_RESERVATION;
    int SlotOffset = InterlockedAdd(g_RWTileBasisCountBuffer[TileIndex], 1);
    if(SlotOffset < TILE_BASIS_INJECTION_RESERVATION) {
        g_RWTileBasisIndexInjectionBuffer[SlotBase + SlotOffset] = BasisIndex;
    }
}

void ScreenCache_AccumulateStepSize (int BasisIndex, SGGradients Step_SG, WGradients Step_W) {
    // Quantilize all step sizes and accumulate to the step buffer
    // Color, Lambda, Normal, WLambda, WAlpha (9)
    uint P0 = QuantilizeRadianceGradient(Step_SG.dColor.x);
    uint P1 = QuantilizeRadianceGradient(Step_SG.dColor.y);
    uint P2 = QuantilizeRadianceGradient(Step_SG.dColor.z);
    uint P3 = QuantilizeLambdaGradient(Step_SG.dLambda);
    uint P4 = QuantilizeLambdaGradient(Step_SG.dDirection.x);
    uint P5 = QuantilizeLambdaGradient(Step_SG.dDirection.y);
    uint P6 = QuantilizeLambdaGradient(Step_SG.dDirection.z);
    uint P7 = QuantilizeLambdaGradient(Step_W.dLambda);
    uint P8 = QuantilizeLambdaGradient(Step_W.dAlpha);
    InterlockedAdd(g_RWQuantilizedBasisStepBuffer[BasisIndex * 9 + 0], P0);
    InterlockedAdd(g_RWQuantilizedBasisStepBuffer[BasisIndex * 9 + 1], P1);
    InterlockedAdd(g_RWQuantilizedBasisStepBuffer[BasisIndex * 9 + 2], P2);
    InterlockedAdd(g_RWQuantilizedBasisStepBuffer[BasisIndex * 9 + 3], P3);
    InterlockedAdd(g_RWQuantilizedBasisStepBuffer[BasisIndex * 9 + 4], P4);
    InterlockedAdd(g_RWQuantilizedBasisStepBuffer[BasisIndex * 9 + 5], P5);
    InterlockedAdd(g_RWQuantilizedBasisStepBuffer[BasisIndex * 9 + 6], P6);
    InterlockedAdd(g_RWQuantilizedBasisStepBuffer[BasisIndex * 9 + 7], P7);
    InterlockedAdd(g_RWQuantilizedBasisStepBuffer[BasisIndex * 9 + 8], P8);
}

// Misc

float3 UniformSampleHemisphere (float2 u) {
    float2 SinCos;
    sincos(2.f * PI * u.x, SinCos.x, SinCos.y);
    float Z = u.y;
    float R = sqrt(max(1.f - Z * Z, 0.f));
    return float3(R * SinCos.y, R * SinCos.x, Z);
}

float3 UniformSampleSphere(float2 u)
{
    float2 SinCos;
    sincos(2.f * PI * u.x, SinCos.x, SinCos.y);
    float Z = 1.f - 2.f * u.y;
    float R = sqrt(max(1.f - Z * Z, 0));
    return float3(R * SinCos.y, R * SinCos.x, Z);
}

float3 CosineWeightedSampleHemisphere (float2 u) {
    float2 SinCos;
    sincos(2.f * PI * u.x, SinCos.x, SinCos.y);
    float Z = sqrt(u.y);
    float R = sqrt(max(1.f - Z * Z, 0.f));
    return float3(R * SinCos.y, R * SinCos.x, Z);
}

float CosineWeightedSampleHemispherePDF (float3 Direction, float3 Normal) {
    return max(0.f, dot(Direction, Normal)) * (1.f / PI);
}

uint DivideAndRoundUp (uint A, uint B) {
    return (A + B - 1) / B;
}

float DepthToLinearDepth (float Depth, float Near, float Far) {
    return Near * Far / (Far - Depth * (Far - Near));
}

float GetLinearDepth(in float depth)
{

    return -g_CameraNear * g_CameraFar / (depth * (g_CameraFar - g_CameraNear) - g_CameraFar);
}

float2 FibonacciLattice (uint i, uint n) {
    float x = 2.23606797749979 * i;
    float y = (float) i / n;
    return float2(x - floor(x), y);
}

float2 FibonacciSpiral (uint i, uint n) {
    float phi = (float) i / n * PI * (3.f - sqrt(5.f));
    float r = sqrt((float) i / n);
    return float2(cos(phi) * r, sin(phi) * r);
}

float3 FibonacciSphere (uint i, uint n) {
    float phi = (float) i / n * PI * (3.f - sqrt(5.f));
    float z = 1.f - (float) i / n * 2.f;
    float r = sqrt(max(0, 1.f - z * z));
    return float3(cos(phi) * r, sin(phi) * r, z);
}

void TangentVectors (float3 Normal, out float3 Tangent, out float3 Bitangent) {
    Bitangent = cross(Normal, float3(0, 0, 1));
    if (dot(Bitangent, Bitangent) < 0.01f) {
        Bitangent = cross(Normal, float3(0, 1, 0));
    }
    Bitangent = normalize(Bitangent);
    Tangent = cross(Bitangent, Normal);
}

float2 UV2NDC2 (float2 UV) {
    return float2(UV.x*2 - 1.f, 1.f - UV.y*2);
}

// Approximating SG integration

struct ASG
{
    float3 Amplitude;
    float3 BasisZ;
    float3 BasisX;
    float3 BasisY;
    float SharpnessX;
    float SharpnessY;
};

float3 EvaluateASG(in ASG asg, in float3 dir)
{
    float sTerm = saturate(dot(asg.BasisZ, dir));
    float lambdaTerm = asg.SharpnessX * dot(dir, asg.BasisX) * dot(dir, asg.BasisX);
    float muTerm = asg.SharpnessY * dot(dir, asg.BasisY) * dot(dir, asg.BasisY);
    return asg.Amplitude * sTerm * exp(-lambdaTerm - muTerm);
}


SGData DistributionTermSG(in float3 direction, in float roughness)
{
    SGData distribution;
    distribution.Direction = direction;
    float m2 = roughness * roughness;
    distribution.Lambda = 2 / m2;
    distribution.Color = 1.0f / (PI * m2);

    return distribution;
}


SGData WarpDistributionSG(in SGData ndf, in float3 view)
{
    SGData warp;
    warp.Direction = reflect(-view, ndf.Direction);
    warp.Lambda = ndf.Lambda;
    warp.Color = ndf.Color;
    warp.Color /= (4.0f * max(dot(ndf.Direction, view), 0.0001f));

    return warp;
}


float GGX_V1(in float m2, in float nDotX)
{
    return 1.0f / (nDotX + sqrt(m2 + (1 - m2) * nDotX * nDotX));
}

float3 ConvolveASG_SG(in ASG asg, in SGData sg) {
    // The ASG paper specifes an isotropic SG as
    // exp(2 * nu * (dot(v, axis) - 1)),
    // so we must divide our SG sharpness by 2 in order
    // to get the nup parameter expected by the ASG formula
    float nu = sg.Lambda * 0.5f;

    ASG convolveASG;
    convolveASG.BasisX = asg.BasisX;
    convolveASG.BasisY = asg.BasisY;
    convolveASG.BasisZ = asg.BasisZ;

    convolveASG.SharpnessX = (nu * asg.SharpnessX) / (nu + asg.SharpnessX);
    convolveASG.SharpnessY = (nu * asg.SharpnessY) / (nu + asg.SharpnessY);

    convolveASG.Amplitude = PI / sqrt((nu + asg.SharpnessX) *
    (nu + asg.SharpnessY));

    float3 asgResult = EvaluateASG(convolveASG, sg.Direction);
    return asgResult * sg.Color * asg.Amplitude;
}

ASG WarpDistributionASG(in SGData ndf, in float3 view)
{
    ASG warp;

    // Generate any orthonormal basis with Z pointing in the
    // direction of the reflected view vector
    warp.BasisZ = reflect(-view, ndf.Direction);
    warp.BasisX = normalize(cross(ndf.Direction, warp.BasisZ));
    warp.BasisY = normalize(cross(warp.BasisZ, warp.BasisX));

    float dotDirO = max(dot(view, ndf.Direction), 0.0001f);

    // Second derivative of the sharpness with respect to how
    // far we are from basis Direction direction
    warp.SharpnessX = ndf.Lambda / (8.0f * dotDirO * dotDirO);
    warp.SharpnessY = ndf.Lambda / 8.0f;

    warp.Amplitude = ndf.Color;

    return warp;
}

float3 SpecularTermASGWarp(in SGData light, in float3 normal,
                           in float roughness, in float3 view)
{
    // Create an SG that approximates the NDF
    SGData ndf = DistributionTermSG(normal, roughness);

    // Apply a warpring operation that will bring the SG from
    // the half-angle domain the the the lighting domain.
    ASG warpedNDF = WarpDistributionASG(ndf, view);

    // Convolve the NDF with the light
    float3 output = ConvolveASG_SG(warpedNDF, light);

    return max(output, 0.0f);
}

SGData CosineLobeSG(in float3 direction)
{
    SGData cosineLobe;
    cosineLobe.Direction = direction;
    cosineLobe.Lambda = 2.133f;
    cosineLobe.Color = 1.17f;

    return cosineLobe;
}

float3 SGInnerProduct(in SGData x, in SGData y)
{
    float dm = length(x.Lambda * x.Direction + y.Lambda * y.Direction);
    float3 expo = exp(dm - x.Lambda - y.Lambda) * x.Color * y.Color;
    float other = 1.0f - exp(-2.0f * dm);
    return (TWO_PI * expo * other) / dm;
}


float3 SGIrradianceInnerProduct(in SGData lightingLobe, in float3 normal)
{
    SGData cosineLobe = CosineLobeSG(normal);
    return max(SGInnerProduct(lightingLobe, cosineLobe), 0.0f);
}

float3 SGDiffuseInnerProduct(in SGData lightingLobe, in float3 normal, in float3 albedo)
{
    float3 brdf = albedo / PI;
    return SGIrradianceInnerProduct(lightingLobe, normal) * brdf;
}

SGData SGInterpolate (in SGData X00, in SGData X01, in SGData X10, in SGData X11, in float2 UV) {
    SGData Result;
    Result.Direction = normalize(lerp(lerp(X00.Direction, X01.Direction, UV.x), lerp(X10.Direction, X11.Direction, UV.x), UV.y));
    Result.Lambda = lerp(lerp(X00.Lambda, X01.Lambda, UV.x), lerp(X10.Lambda, X11.Lambda, UV.x), UV.y);
    Result.Color = lerp(lerp(X00.Color, X01.Color, UV.x), lerp(X10.Color, X11.Color, UV.x), UV.y);
    return Result;
}

// Removes NaNs from the color values.
float GIDenoiser_RemoveNaNs(in float color)
{
    color /= (1.0f + color);
    color  = saturate(color);
    color /= max(1.0f - color, 1e-4f);
    return color;
}

// Removes NaNs from the color values.
float3 GIDenoiser_RemoveNaNs(in float3 color)
{
    color /= (1.0f + color);
    color  = saturate(color);
    color /= max(1.0f - color, 1e-4f);
    return color;
}

// Removes NaNs from the color values.
float4 GIDenoiser_RemoveNaNs(in float4 color)
{
    color /= (1.0f + color);
    color  = saturate(color);
    color /= max(1.0f - color, 1e-4f);
    return color;
}

#endif // MIGI_SHARED_HLSL