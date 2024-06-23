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

#include "../../math/spherical_harmonics.hlsl"

#include "../../geometry/mesh.hlsl"
#include "../../geometry/intersection.hlsl"

float ClipFp16 (float Value) {
    return clamp(Value, -60000.f, 60000.f);
}

float2 ClipFp16 (float2 Value) {
    return clamp(Value, -60000.f.xx, 60000.f.xx);
}

float3 ClipFp16 (float3 Value) {
    return clamp(Value, -60000.f.xxx, 60000.f.xxx);
}

float4 ClipFp16 (float4 Value) {
    return clamp(Value, -60000.f.xxxx, 60000.f.xxxx);
}

// Resolve directional shift for quantilized normal
float3 lazyNormalize (float3 n) {
    if(abs(dot(n, n) - 1.f) < 0.01f) {
        return n;
    }
    return normalize(n);
}

// Packing and unpacking misc
float3 UnpackFp16x3 (uint2 v) {
    return float3(f16tof32(v.x & 0xFFFF), f16tof32(v.x >> 16), f16tof32(v.y & 0xFFFF));
}
float4 UnpackFp16x4 (uint2 v) {
    return float4(f16tof32(v.x & 0xFFFF), f16tof32(v.x >> 16), f16tof32(v.y & 0xFFFF), f16tof32(v.y >> 16));
}

uint2 PackFp16x3Safe (float3 v) {
    v = ClipFp16(v);
    return uint2(f32tof16(v.x) | (f32tof16(v.y) << 16), f32tof16(v.z));
}

uint2 PackFp16x4Safe (float4 v) {
    v = ClipFp16(v);
    return uint2(f32tof16(v.x) | (f32tof16(v.y) << 16), f32tof16(v.z) | (f32tof16(v.w) << 16));
}

uint PackUint16x2 (uint2 v) {
    return v.x | (v.y << 16);
}

uint2 UnpackUint16x2 (uint v) {
    return uint2(v & 0xFFFF, v >> 16);
}

// Copy pasted from Lumen
float2 Hammersley16( uint Index, uint NumSamples, uint2 Random )
{
	float E1 = frac( (float)Index / NumSamples + float( Random.x ) * (1.0 / 65536.0) );
	float E2 = float( ( reversebits(Index) >> 16 ) ^ Random.y ) * (1.0 / 65536.0);
	return float2( E1, E2 );
}

// to [-1, 1]^2
float2 UnitVectorToOctahedron(float3 N)
{
	N.xy /= dot( 1, abs(N) );
	if( N.z <= 0 )
	{
		N.xy = ( 1 - abs(N.yx) ) * select( N.xy >= 0, float2(1,1), float2(-1,-1) );
	}
	return N.xy;
}

// from [-1, 1]^2
float3 OctahedronToUnitVector( float2 Oct )
{
	float3 N = float3( Oct, 1 - dot( 1, abs(Oct) ) );
	float t = max( -N.z, 0 );
	N.xy += select(N.xy >= 0, float2(-t, -t), float2(t, t));
	return normalize(N);
}


// Project a point in screen space to world space
// Transform: InvViewProj
float3 InverseProject(in float4x4 transform, in float2 uv, in float depth)
{
    float4 homogeneous = mul(transform, float4(2.0f * float2(uv.x, 1.0f - uv.y) - 1.0f, depth, 1.0f));
    return homogeneous.xyz / homogeneous.w; // perspective divide
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
    float  Lambda;
    float3 Color;
    float  Depth;
};

float OneSubExpNeg2Lambda (float lambda) {
    return lambda < 24.f ? (1.f - exp(-2.f * lambda)) : 1.f;
}

// Return the integral of a SG
float SGIntegrate (float lambda) {
    return (TWO_PI * OneSubExpNeg2Lambda(lambda)) / lambda;
}

float SGNormalizationFactor (float lambda) {
    return lambda / (TWO_PI * OneSubExpNeg2Lambda(lambda));
}

float3 EvaluateSG(SGData SG, float3 Direction)
{
    return SG.Color * exp(SG.Lambda * (dot(SG.Direction, Direction) - 1.f));
}

float  EvaluateSGRaw (SGData SG, float3 Direction) {
    return exp(SG.Lambda * (dot(SG.Direction, Direction) - 1.f));
}

float EvaluateNormalizedSG (SGData SG, float3 Direction) {
    float raw = exp(SG.Lambda * (dot(SG.Direction, Direction) - 1.f));
    // Normalize the SG to make its integral equals to 1
    return raw * SGNormalizationFactor(SG.Lambda);
}

// FDist: distance in pixels
float EvaluateFilmCoverage (float2 FDist) {
    float Radius = 4.f;
    float Sqr = dot(FDist, FDist);
    return exp(- Sqr * (4.f / (Radius * Radius)) );
}

struct SGGradients {
    float dLambda;
    float3 dColor;
    float3 dDirection;
};

void EvaluateSG_Gradients (SGData SG, float3 TargetDirection, out SGGradients Gradients, out float3 dColorExtra) {
    // Compute the Gradients for SG parameters
    // Targeting at the remaining radiance after subtracting all other SH and SGs
    float W1 = dot(SG.Direction, TargetDirection) - 1;
    float W2 = exp(SG.Lambda * W1);
    float W3 = W1 * W2;
    float SGColorScale = dot(SG.Color, 1.f.xxx);
    Gradients.dLambda    = W3 * SGColorScale;
    Gradients.dColor     = W2.xxx;

    Gradients.dDirection = TargetDirection * W2 * SGColorScale * SG.Lambda;
    dColorExtra = 0.f.xxx;
}

int QuantilizeNormGradient (float V, float Noise) {
#ifdef HEURISTIC_DIRECTION_UPDATE
    return floor(V * 32768.f + Noise);
#else
    return floor(V * 262144.f + Noise);
#endif
}
float RecoverNormGradient (int V) {
#ifdef HEURISTIC_DIRECTION_UPDATE
    return float(V) / 32768.f;
#else
    return float(V) / 262144.f;
#endif
}
int QuantilizeRadianceGradient (float Radiance, float Noise) {
    Radiance = min(abs(Radiance), 2000.f) * sign(Radiance);
    return floor(Radiance * 512.f + Noise);
}
float RecoverRadianceGradient (int Radiance) {
    return float(Radiance) / 512.f;
}
int QuantilizeLambdaGradient (float dL, float Noise) {
    return floor(dL * 65536.f + Noise);
}
float RecoverLambdaGradient (int dL) {
    return float(dL) / 65536.f;
}

int QuantilizeAlphaGradient (float Alpha, float Noise) {
    return floor(Alpha * 10000.f + Noise);
}
float RecoverAlphaGradient (int Alpha) {
    return float(Alpha) / 10000.f;
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

uint4 FetchBasisData_Packed (int BasisIndex, bool bPrevious = false) {
    uint P0 = bPrevious ? g_RWPreviousProbeSGBuffer[BasisIndex * 4] : g_RWProbeSGBuffer[BasisIndex * 4];
    uint P1 = bPrevious ? g_RWPreviousProbeSGBuffer[BasisIndex * 4 + 1] : g_RWProbeSGBuffer[BasisIndex * 4 + 1];
    uint P2 = bPrevious ? g_RWPreviousProbeSGBuffer[BasisIndex * 4 + 2] : g_RWProbeSGBuffer[BasisIndex * 4 + 2];
    uint P3 = bPrevious ? g_RWPreviousProbeSGBuffer[BasisIndex * 4 + 3] : g_RWProbeSGBuffer[BasisIndex * 4 + 3];
    return uint4(P0, P1, P2, P3);
}

float3 UnpackNormal (uint Packed) {
    // return unpackUnorm4x8(Packed).xyz * 2.f - 1.f;
    uint3 Dir = uint3(Packed & 0x3ff, (Packed >> 10) & 0x3ff, (Packed >> 20) & 0x3ff);
    return (float3(Dir) + (1.f / 0x800)) * (2.f / 0x400) - 1.f;
}

uint PackNormal (float3 Normal) {
    // return packUnorm4x8(float4(Normal * 0.5f + 0.5f, 0.f));
    uint3 Dir = clamp((Normal + 1.f) * (0x200), 0, 0x3ff);
    return Dir.x | (Dir.y << 10) | (Dir.z << 20);
}

uint PackUnorm2x16Unbiased (float2 V) {
    uint2 packedValue = min(uint2(saturate(V) * 65536.0f), 65535);
    return packedValue.x | (packedValue.y << 16);
}

float2 UnpackUnorm2x16Unbiased (uint Packed) {
    return float2((Packed & 0xFFFF) / 65536.0f + 0.5f / 65536.f, (Packed >> 16) / 65536.0f + 0.5f / 65536.f);
}

SGData UnpackBasisData (uint4 Packed) {
    SGData SG;
    SG.Color     = float3(f16tof32(Packed.x & 0xFFFF), f16tof32(Packed.x >> 16), f16tof32(Packed.y & 0xFFFF));
    SG.Lambda    = f16tof32(Packed.y >> 16);
    SG.Direction = OctahedronToUnitVector(UnpackUnorm2x16Unbiased(Packed.z) * 2 - 1);
    SG.Depth     = asfloat(Packed.w);
    return SG;
}

uint4 PackBasisData (SGData SG) {
    uint4 Packed;
    SG.Color  = ClipFp16(SG.Color);
    SG.Lambda = ClipFp16(SG.Lambda);
    Packed.x = f32tof16(SG.Color.x) | (f32tof16(SG.Color.y) << 16);
    Packed.y = f32tof16(SG.Color.z) | (f32tof16(SG.Lambda) << 16);
    // TODO oct encode
    Packed.z = PackUnorm2x16Unbiased(UnitVectorToOctahedron(SG.Direction) * 0.5 + 0.5);
    Packed.w = asuint(SG.Depth);
    return Packed;
}

void WriteBasisData (int BasisIndex, SGData SG) {
    uint4 Packed = PackBasisData(SG);
    g_RWProbeSGBuffer[BasisIndex * 4] = Packed.x;
    g_RWProbeSGBuffer[BasisIndex * 4 + 1] = Packed.y;
    g_RWProbeSGBuffer[BasisIndex * 4 + 2] = Packed.z;
    g_RWProbeSGBuffer[BasisIndex * 4 + 3] = Packed.w;
}


SGData FetchBasisData (int BasisIndex, bool bPrevious = false) {
    uint4 Packed = FetchBasisData_Packed(BasisIndex, bPrevious);
    return UnpackBasisData(Packed);
}

float3 FetchUpdateRayDirection (int RayIndex) {
    uint Packed = g_RWUpdateRayDirectionBuffer[RayIndex];
    return OctahedronToUnitVector(UnpackUnorm2x16Unbiased(Packed) * 2.f - 1.f);
}

void WriteUpdateRay(int2 ProbeIndex, int2 ProbeScreenPosition, int RayRank, float3 RayDirection, float RayPdf) {
    int ProbeIndex1 = ProbeIndex.x + ProbeIndex.y * MI.TileDimensions.x;
    int BaseOffset = g_RWProbeUpdateRayOffsetBuffer[ProbeIndex1];
    int RayIndex = BaseOffset + RayRank;
    if(WaveIsFirstLane()) g_RWUpdateRayProbeBuffer[RayIndex / WAVE_SIZE] = PackUint16x2(ProbeIndex);
    g_RWUpdateRayDirectionBuffer[RayIndex] = PackUnorm2x16Unbiased(UnitVectorToOctahedron(RayDirection) * 0.5 + 0.5);
    g_RWUpdateRayRadianceInvPdfBuffer[RayIndex] = PackFp16x4Safe(float4(0.f.xxx, RayPdf == 0 ? 0 : (1.f / RayPdf)));
}

// Misc

float3 UniformSampleHemisphere (float2 u) {
    float2 SinCos;
    sincos(2.f * PI * u.x, SinCos.x, SinCos.y);
    float Z = u.y;
    float R = sqrt(max(1.f - Z * Z, 0.f));
    return float3(R * SinCos.y, R * SinCos.x, Z);
}

float UniformSampleHemispherePdf ()
{
    return 1.f / (2.f * PI);
}

float3 UniformSampleSphere(float2 u)
{
    float2 SinCos;
    sincos(2.f * PI * u.x, SinCos.x, SinCos.y);
    float Z = 1.f - 2.f * u.y;
    float R = sqrt(max(1.f - Z * Z, 0));
    return float3(R * SinCos.y, R * SinCos.x, Z);
}

float UniformSampleSpherePdf ()
{
    return 1.f / (4.f * PI);
}

float3 CosineWeightedSampleHemisphere (float2 u) {
    float2 SinCos;
    sincos(2.f * PI * u.x, SinCos.x, SinCos.y);
    float Z = sqrt(u.y);
    float R = sqrt(max(1.f - Z * Z, 0.f));
    return float3(R * SinCos.y, R * SinCos.x, Z);
}

float CosineWeightedSampleHemispherePDF (float CosTheta) {
    return CosTheta * (1.f / PI);
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

float GetLinearDepth(in float depth, bool bPrevious = false)
{
    if(bPrevious) return -MI.PreviousCameraNear * MI.PreviousCameraFar / (depth * (MI.PreviousCameraFar - MI.PreviousCameraNear) - MI.PreviousCameraFar);
    else return -MI.CameraNear * MI.CameraFar / (depth * (MI.CameraFar - MI.CameraNear) - MI.CameraFar);
}

float2 FibonacciLattice (uint i, uint n) {
    float x = 2.23606797749979f * i;
    float y = (float) i / n;
    return float2(x - floor(x), y);
}

float2 FibonacciSpiral (uint i, uint n) {
    float phi = (float) i / n * PI * (3.f - sqrt(5.f));
    float r = sqrt((float) i / n);
    return float2(cos(phi * i) * r, sin(phi * i) * r);
}

float3 FibonacciSphere (uint i, uint n) {
    float phi = float(i) / n * (PI * (3.f - sqrt(5.f)));
    float z = 1.f - (float) i / n * 2.f;
    float r = sqrt(max(0, 1.f - z * z));
    return float3(cos(phi * i) * r, sin(phi * i) * r, z);
}

// TODO can be better?
float3 InitHemiDirections (int i, int n) {
    if(n == 1) return float3(0, 0, 1);
    if(n == 2) {
        return i == 0 ? float3(0.5, 0, 0.866025403784438f) : float3(-0.5, 0, 0.866025403784438f);
    }
    if(n == 4) {
        float v = sqrt(2.f) / 2.f;
        switch(i) {
            case 0: return float3(v, 0, v);
            case 1: return float3(-v, 0, v);
            case 2: return float3(0, v, v);
            case 3: return float3(0, -v, v);
        }
    }
    return FibonacciSphere(i, n*2);
}

// Replaced with the better impl from GI10 (GetOrthoVectors)
// void TangentVectors (float3 Normal, out float3 Tangent, out float3 Bitangent) {
//     float3 AbsNormal = abs(Normal);
//     if(AbsNormal.x < AbsNormal.y && AbsNormal.x < AbsNormal.z) {
//         Bitangent = cross(Normal, float3(1, 0, 0));
//     } else if(AbsNormal.y < AbsNormal.z) {
//         Bitangent = cross(Normal, float3(0, 1, 0));
//     } else {
//         Bitangent = cross(Normal, float3(0, 0, 1));
//     }
//     Bitangent = normalize(Bitangent);
//     Tangent = normalize(cross(Bitangent, Normal));
// }



float2 UV2NDC2 (float2 UV) {
    return float2(UV.x*2 - 1.f, 1.f - UV.y*2);
}
float2 NDC22UV (float2 NDC2) {
    return float2(NDC2.x*0.5f + 0.5f, 0.5f - NDC2.y*0.5f);
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

// TODO improve this
SGData CombineSG (SGData SG1, SGData SG2) {
    float W1 = max(SGIntegrate(SG1.Lambda) * dot(SG1.Color, 1.f.xxx), 0) + 1e-8f;
    float W2 = max(SGIntegrate(SG2.Lambda) * dot(SG2.Color, 1.f.xxx), 0) + 1e-8f;
    SGData Result;
    Result.Direction = normalize(SG1.Direction*W1 + SG2.Direction*W2);
    float DirectionNorm = dot(Result.Direction, Result.Direction);
    // TODO: This may be slow
    if(isnan(DirectionNorm) || DirectionNorm < 1e-4f || isinf(DirectionNorm)) {
        Result.Direction = W1 > W2 ? SG1.Direction : SG2.Direction;
    }
    Result.Lambda = (SG1.Lambda*W1 + SG2.Lambda*W2) / (W1 + W2);
    Result.Color  = (SG1.Color*W1 + SG2.Color*W2) / (W1 + W2);
    Result.Depth  = (SG1.Depth*W1 + SG2.Depth*W2) / (W1 + W2);
    float W3 = max(SGIntegrate(Result.Lambda) * dot(Result.Color, 1.f.xxx), 0) + 1e-8f;
    Result.Color *=  (W1 + W2) / W3;
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

// Color mapping for debugging
// Map 1 channel to heat (R - G - B)
float3 ColorHeatMap (float h) {
    float H = saturate(1.0f - h) * 5.0f;
    float R = saturate(min(H - 1.5f, 4.5f - H));
    float G = saturate(min(H - 0.5f, 3.5f - H));
    float B = saturate(min(H + 0.5f, 2.5f - H));
    return float3(R, G, B);
}

float3 BasisIndexToColor (int BasisIndex) {
    const float3 _BasisIndexColorMap[15] = {
        float3(1, 0, 0),
        float3(0, 1, 0),
        float3(0, 0, 1),
        float3(1, 1, 0),
        float3(1, 0, 1),
        float3(0, 1, 1),
        float3(1, 0.5, 0),
        float3(0, 1, 0.5),
        float3(0.5, 0, 1),
        float3(1, 0, 0.5),
        float3(0, 0.5, 1),
        float3(0.5, 1, 0),
        float3(1, 0.5, 0.5),
        float3(0.5, 1, 0.5),
        float3(0.5, 0.5, 1)
    };
    return _BasisIndexColorMap[BasisIndex % 15];
}


// G-Buffer ops

float3 RecoverWorldPositionHiRes (int2 TexCoords) {
    float4 Visibility   = g_VisibilityTexture[TexCoords];
    float2 Barycentrics = Visibility.xy;
    uint   InstanceID   = asuint(Visibility.z);
    uint   PrimitiveID  = asuint(Visibility.w);
    Instance InstanceData = g_InstanceBuffer[InstanceID];
    Mesh     mesh     = g_MeshBuffer[InstanceData.mesh_index];

    Triangle vertices = fetchVertices(mesh, PrimitiveID);

    // Reconstruct world space position from barycentrics
    float3x4 transform = g_TransformBuffer[InstanceData.transform_index];
    vertices.v0 = transformPoint(vertices.v0, transform);
    vertices.v1 = transformPoint(vertices.v1, transform);
    vertices.v2 = transformPoint(vertices.v2, transform);

    float3 WorldPosition = interpolate(vertices.v0, vertices.v1, vertices.v2, Barycentrics);
    return WorldPosition; 
}

float3 InterpolateDirection (float3 X, float3 Y, float A) {
    float CosTheta = dot(X, Y);
    float Theta = acos(CosTheta);
    float Phi1  = A * Theta;
    float Phi2  = Theta - Phi1;
    return normalize(Y * tan(Phi1) + X * tan(Phi2));
}

// Quantilization
// May overflow if the radiance is too large (e.g. 5000)
int QuantilizeRadiance (float V, float Noise) {
    return floor(sign(V) * min(abs(V), 5000) * MIGI_QUANTILIZE_RADIANCE_MULTIPLIER + Noise);
}
float RecoverRadiance (int V) {
    return float(V) / MIGI_QUANTILIZE_RADIANCE_MULTIPLIER;
}
float3 RecoverRadiance (int3 V) {
    return float3(V) / MIGI_QUANTILIZE_RADIANCE_MULTIPLIER;
}
float4 RecoverRadiance (int4 V) {
    return float4(V) / MIGI_QUANTILIZE_RADIANCE_MULTIPLIER;
}
int QuantilizeWeight (float V, float Noise) {
    // 2^17 = 131,072 (fp32: 2^23 precision)
    // Note: InvPdf < 100, no worries about overflowing
    return floor(V * 131072.f + Noise);
}
float RecoverWeight (int V) {
    return float(V) / 131072.f;
}


#endif // MIGI_SHARED_HLSL