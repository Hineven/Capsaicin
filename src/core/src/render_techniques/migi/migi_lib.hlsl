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
};
float EvaluateW (WData WD, float2 Delta)
{
    return max(dot(4 - max(abs(Delta.x), abs(Delta.y)), float2(1, 1)), 0) * 0.1f;//exp(-dot(Delta, Delta) * WD.Lambda);
}

struct SGDifferentials {
    float dLambda;
    float3 dColor;
    float3 dDirection;
};

// error function: (x - y) ^ 2
void EvaluateSGDifferentials2 (SGData SG, float3 TargetDirection, float3 TargetRadiance, float3 CurrentRadiance, out SGDifferentials Differentials) {
    // Compute the differentials for SG parameters
    // Targeting at the remaining radiance after subtracting all other SH and SGs
    float W1 = (dot(SG.Direction, TargetDirection) - 1);
    float3 W2 = exp(SG.Lambda * W1);
    float3 W3 = 2 * W2 * (SG.Color * W2 - TargetRadiance);
    // d (sg_theta(dir) - v)^2 / d theta
    Differentials.dLambda = dot(SG.Color * W3 * (dot(SG.Direction, TargetDirection) - 1.f), float3(1, 1, 1));
    Differentials.dColor = W3;
    Differentials.dDirection = TargetDirection * dot(SG.Color * SG.Lambda * W3, float3(1, 1, 1));
}

// error function: abs(x - y)
void EvaluateSGDifferentials (SGData SG, float3 TargetDirection, float3 TargetRadiance, float3 CurrentRadiance, out SGDifferentials Differentials) {
    // Compute the differentials for SG parameters
    // Targeting at the remaining radiance after subtracting all other SH and SGs
    float W1 = dot(SG.Direction, TargetDirection) - 1;
    float W2 = exp(SG.Lambda * W1);
    float W3 = W1 * W2;
    float TargetRadianceScale = dot(TargetRadiance, float3(1, 1, 1));
    float CurrentRadianceScale = dot(CurrentRadiance, float3(1, 1, 1));
    float SGColorScale = dot(SG.Color, float3(1, 1, 1));
    float DiffDirection1 = CurrentRadianceScale - TargetRadianceScale;// CurrentRadianceScale > TargetRadianceScale ? 1 : -1;
    float3 DiffDirection3 = CurrentRadiance - TargetRadiance;// select(CurrentRadiance > TargetRadiance, 1.f.xxx, -1.f.xxx);
    Differentials.dLambda    = W3 * SGColorScale * DiffDirection1;
    Differentials.dColor     = W2 * DiffDirection3;
    Differentials.dDirection = TargetDirection * W2 * SGColorScale * SG.Lambda * DiffDirection1;
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

void FetchBasisData (int2 PixelID, out SGData SG, out WData WD) {
    float4 P0 = g_RWBasisParameterTexture[PixelID];
    float4 C0 = g_RWBasisColorTexture[PixelID];
    SG.Direction = P0.xyz;
    SG.Lambda = P0.w;
    SG.Color = C0.xyz;
    WD.Lambda = 0.25f;
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
    Tangent = cross(Bitangent, Normal);
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

// Screen Basis
// struct BasisData {
//     // Counter-clockwise polygon vertices
//     // They should include depth (calculated from frame to frame)
//     int2 Offset, Size;
//     // texture level (8) / Offset of the texture brick (12, 12) in the atlas
//     // Avaliable: 0: 1x1, 1: 2x2, 2: 4x4, 3: 8x8, 4: 16x16
//     uint TextureLevelOffset;
//     // The depth of the basis (0-1, NDC space)
//     float Depth;
//     // The depth kernel size of the basis
//     float DepthRange;
// };
//
// void FetchBasis (int BasisIndex, out BasisData Basis) {
//     uint4 BasisDataPacked = g_BasisBuffer[BasisIndex];
//     Basis.Offset = UnpackUInt2x16(BasisDataPacked.x);
//     Basis.Size = UnpackUInt2x16(BasisDataPacked.y);
//     Basis.TextureLevelOffset = BasisDataPacked.z;
//     Basis.Depth = ashalf(BasisDataPacked.w&0xffffu);
//     Basis.DepthRange = ashalf((BasisDataPacked.w>>16u)&0xffffu);
// }
//
// void UnpackTextureLevelOffset (uint TextureLevelOffset, int TextureLevel, int2 TextureOffset) {
//     TextureLevel = TextureLevelOffset >> 24u;
//     TextureOffset = int2(TextureLevelOffset & 0xFFFu, (TextureLevelOffset >> 12) & 0xFFFu);
// }
//
// void PackTextureLevelOffset (int TextureLevel, int2 TextureOffset) {
//     return (TextureLevel << 24u) | (TextureOffset.x & 0xFFFu) | ((TextureOffset.y & 0xFFFu) << 12u);
// }
//
// void FetchBasisBrickTexel (int TextureLevelOffset, int2 Texel, out SGData SG, out float EvaluatedW) {
//     int TextureLevel, TextureOffset;
//     UnpackTextureLevelOffset(TextureLevelOffset, TextureLevel, TextureOffset);
//     float4 Params = g_BasisAtlasParameter[TextureLevel].Load(int3(TextureOffset + Texel, 0));
//     float3 Color  = g_BasisAtlasColor[TextureLevel].Load(int3(TextureOffset + Texel, 0));
//     SG = SGData{Params.xyz, Params.w, Color};
//     EvaluatedW = g_BasisAtlasW[TextureLevel].Load(int3(TextureOffset + Texel, 0)).x;
// }
//
// void SampleBasisBrickTexel (int TextureLevelOffset, float2 UV, out SGData SG, out float EvaluatedW) {
//     int TextureLevel, TextureOffset;
//     UnpackTextureLevelOffset(TextureLevelOffset, TextureLevel, TextureOffset);
//     float2 RealUV = TextureOffset * g_InvBasisAtlasSize[TextureLevel] + UV * float2(g_BasisAtlasUVScaler[TextureLevel]);
//     float4 Params = g_BasisAtlasParameter[TextureLevelBrick].SampleLevel(g_TextureSampler, RealUV, 0.0f);
//     float3 Color  = g_BasisAtlasColor[TextureLevelBrick].SampleLevel(g_TextureSampler, RealUV, 0.0f);
//     SG = SGData{Params.xyz, Params.w, Color};
//     EvaluatedW = g_BasisAtlasW[TextureLevelBrick].SampleLevel(g_TextureSampler, RealUV, 0.0f).x;
// }
//
// // @param MN The fractional part of the texture coordinates
// // @param BrickTextureCoords The integer part of the texture coordinates within the basis texture brick
// void SampleBasisBrickTexel (int TextureLevelOffset, float2 UV, out SGData SG, out float EvaluatedW, out float2 MN, out int2 BrickTextureCoords) {
//     int TextureLevel, TextureOffset;
//     UnpackTextureLevelOffset(TextureLevelOffset, TextureLevel, TextureOffset);
//     float2 RealUV = TextureOffset * g_InvBasisAtlasSize[TextureLevel] + UV * float2(g_BasisAtlasUVScaler[TextureLevel]);
//     float2 XY = RealUV * g_BasisAtlasSize[TextureLevel];
//     // The parameters for tri-linear interpolation
//     MN = frac(XY);
//     BrickTextureCoords = int2(UV * float(1<<TextureLevel));
//     float4 Params = g_BasisAtlasParameter[TextureLevelBrick].SampleLevel(g_TextureSampler, RealUV, 0.0f);
//     float3 Color  = g_BasisAtlasColor[TextureLevelBrick].SampleLevel(g_TextureSampler, RealUV, 0.0f);
//     SG = SGData{Params.xyz, Params.w, Color};
//     EvaluatedW = g_BasisAtlasW[TextureLevelBrick].SampleLevel(g_TextureSampler, RealUV, 0.0f).x;
// }
//
// float2 ConvertUVScreenToBasis (BasisData Basis, float2 UV) {
//     float XY = (UV - float(Basis.Offset) + 0.5) / Basis.Size;
//     return XY;
// }
//
// void EvaluateBasis (BasisData Basis, float2 UV, float Depth, out SGData SG, out float EvaluatedW) {
//     float2 XY = ConvertUVScreenToBasis(Basis, UV);
//     if(XY < 0 || XY > 1) {
//         EvaluatedW = 0.f;
//     } else SampleBasisBrickTexel(Basis.TextureLevelOffset, XY, SG, EvaluatedW);
// }
//
// // It's the caller's duty to spread the differentials from tri-linear interpolation to the neighboring texels
// // Use group-shared memory to accumulate differentials for acceleration.
// // Note: the output SG differentials are not multiplied by EvaluatedW. Don't forget that!
// // @param MN: The fractional part of the texture coordinates
// // @param BrickTextureCoords: The integer part of the texture coordinates within the basis texture brick
// void GetBasisUpdateDifferentials (BasisData Basis, float2 UV, float3 Direction, float3 TargetRadiance,
//     out float EvaluatedW,
//     out SGDifferentials Differentials, out float WDifferential,
//     out float2 MN, out int2 BrickTextureCoords) {
//     SGData SG;
//     float2 XY = ConvertUVScreenToBasis(Basis, UV);
//     if(XY < 0 || XY > 1) {
//         EvaluatedW = 0.f;
//     } else SampleBasisBrickTexel(Basis.TextureLevelOffset, XY, SG, EvaluatedW, MN, BrickTextureCoords);
//     float3 CurrentRadiance = EvaluateSG(SG, Direction) * EvaluatedW;
//     EvaluateSGDifferentials(SG, Direction, TargetRadiance, Differentials);
//     WDifferential = dot(CurrentRadiance - TargetRadiance, 1.xxx);
// }
//
// float EvaluateSGSimilarity (SGData X, float W_X, SGData Y, float W_Y) {
//     float3 X_Eval = EvaluateSG(X, X.Direction);
//     float3 Y_Eval = EvaluateSG(Y, Y.Direction);
//     return dot(X_Eval, Y_Eval) * W_X * W_Y;
// }


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