/*
 * Project Capsaicin: migi_internal.h
 * Created: 2024/5/18
 * This program uses MulanPSL2. See LICENSE for more.
 */

#ifndef CAPSAICIN_MIGI_INTERNAL_H
#define CAPSAICIN_MIGI_INTERNAL_H
#include <cstdint>
namespace Capsaicin {
inline int divideAndRoundUp (int a, int b) {
    return (a + b - 1) / b;
}
inline int divideAndRoundUp (uint32_t a, int b) {
    return ((int)a + b - 1) / b;
}
inline uint32_t divideAndRoundUp (uint32_t a, uint32_t b) {
    return (a + b - 1u) / b;
}
}

#endif // CAPSAICIN_MIGI_INTERNAL_H
