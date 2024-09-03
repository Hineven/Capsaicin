/*
 * Created: 2024/9/2
 * Author:  hineven
 * See LICENSE for licensing.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <glm/glm.hpp>
#include <corecrt_math_defines.h>
using namespace glm;

float EvaluateSGRaw (vec3 sg_direction, float lambda, vec3 direction) {
    return exp(lambda * (dot(sg_direction, direction) - 1.f));
}

vec2 mapToHemiOctahedron(vec3 direction)
{
    // Modified version of "Fast Equal-Area Mapping of the (Hemi)Sphere using SIMD" - Clarberg
    vec3 absDir = abs(direction);

    float radius = sqrt(1.0f - absDir.z);
    float a = max(absDir.x, absDir.y);
    float b = min(absDir.x, absDir.y);
    b = a == 0.0f ? 0.0f : b / a;

    float phi = atan(b) * (2.0f / M_PI);
    phi = (absDir.x >= absDir.y) ? phi : 1.0f - phi;

    float t = phi * radius;
    float s = radius - t;
    vec2 st = vec2(s, t);
    st *= vec2(sign(direction));

    // Since we only care about the hemisphere above the surface we rescale and center the output
    //   value range to the it occupies the whole unit square
    st = vec2(st.x + st.y, st.x - st.y);

    // Transform from [-1,1] to [0,1]
    st = 0.5f * st + 0.5f;

    return st;
}

vec2 UnitVectorToHemiOctahedron01( vec3 N )
{
    return mapToHemiOctahedron(N);
}

vec3 mapToHemiOctahedronInverse(vec2 mapped)
{
    // Transform from [0,1] to [-1,1]
    vec2 st = 2.0f * mapped - 1.0f;

    // Transform from unit square to diamond corresponding to +hemisphere
    st = vec2(st.x + st.y, st.x - st.y) * 0.5f;

    vec2 absMapped = abs(st);
    float distance = 1.0f - (absMapped.x + absMapped.y);
    float radius = 1.0f - abs(distance);

    float phi = (radius == 0.0f) ? 0.0f : M_PI_4 * ((absMapped.y - absMapped.x) / radius + 1.0f);
    float radiusSqr = radius * radius;
    float sinTheta = radius * sqrt(2.0f - radiusSqr);
    float sinPhi, cosPhi;
    sinPhi = sin(phi);
    cosPhi = cos(phi);
    float x = sinTheta * sign(st.x) * cosPhi;
    float y = sinTheta * sign(st.y) * sinPhi;
    float z = sign(distance) * (1.0f - radiusSqr);

    return vec3(x, y, z);
}

vec3 HemiOctahedron01ToUnitVector (vec2 oct) {
    return mapToHemiOctahedronInverse(oct);
}

#define OCT_RESOLUTION 8

int main () {
    vec3 direction = vec3(0, 0, 1);
    vec3 sg_direction = vec3(0.2, 0.1, 1);
    sg_direction = normalize(sg_direction);
    direction = normalize(direction);
    float sg_lambda = 100.f;
    for(int x = 0; x < OCT_RESOLUTION; x ++) {
        for(int y = 0; y < OCT_RESOLUTION; y ++) {
            vec3 test_direction = HemiOctahedron01ToUnitVector((vec2(x, y) + 0.5f) * (1.f / OCT_RESOLUTION));
            float sg_value = EvaluateSGRaw(sg_direction, sg_lambda, test_direction);
            printf("%.3f\n", sg_value);
        }
    }
}