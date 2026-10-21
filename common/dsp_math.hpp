// Small math helpers shared by all units.
// Kept local so the firmware builds never link libm, and so host renders
// match the hardware bit for bit (same approximations).

#pragma once

#include <stdint.h>

#if defined(__arm__)
#define DSPM_INLINE static inline __attribute__((always_inline, optimize("Ofast")))
#define DSPM_INLINE_MEMBER inline __attribute__((always_inline, optimize("Ofast")))
#else
#define DSPM_INLINE static inline __attribute__((always_inline))
#define DSPM_INLINE_MEMBER inline __attribute__((always_inline))
#include <cmath>
#endif

namespace dspm {

DSPM_INLINE float floorf_(float x) {
  const float t = (float)(int32_t)x;
  return (t > x) ? t - 1.f : t;
}

DSPM_INLINE float fabsf_(float x) { return x < 0.f ? -x : x; }

DSPM_INLINE float clampf_(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

DSPM_INLINE float sqrtf_(float x) {
#if defined(__arm__)
  float r;
  __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
  return r;
#else
  return std::sqrt(x);
#endif
}

// sin(2*pi*t), t in turns, any range. Max error around 1e-7.
DSPM_INLINE float sin_turns(float t) {
  t -= floorf_(t + 0.5f);  // [-0.5, 0.5)
  if (t > 0.25f)
    t = 0.5f - t;
  else if (t < -0.25f)
    t = -0.5f - t;  // [-0.25, 0.25]
  const float x = t * 6.283185307f;
  const float x2 = x * x;
  return x * (1.f + x2 * (-1.6666667e-1f +
                          x2 * (8.3333333e-3f +
                                x2 * (-1.9841270e-4f + x2 * 2.7557319e-6f))));
}

DSPM_INLINE float cos_turns(float t) { return sin_turns(t + 0.25f); }

// Cubic soft clip, flat above |x| = 1.5.
DSPM_INLINE float softclip(float x) {
  x = clampf_(x, -1.5f, 1.5f);
  return x - (4.f / 27.f) * x * x * x;
}

// 2^x for x in [-24, 24]. Cubic on the fraction, max error ~1e-4.
DSPM_INLINE float exp2f_(float x) {
  x = clampf_(x, -24.f, 24.f);
  const float fl = floorf_(x);
  const float f = x - fl;
  const float p = 1.f + f * (0.6951786f + f * (0.2261929f + f * 0.0786281f));
  const int32_t e = (int32_t)fl;
  union {
    float f;
    uint32_t i;
  } u;
  u.i = (uint32_t)(e + 127) << 23;  // 2^e
  return u.f * p;
}

}  // namespace dspm
