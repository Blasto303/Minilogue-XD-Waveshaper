// Waveshaper oscillator DSP core.
//
// Plain C++11, no logue SDK dependency, so the same code runs on the
// minilogue xd and in the host renderer (tools/render).
//
// Signal path, per sample:
//   source (sine <-> triangle morph, + optional sub sine one octave down)
//   -> gain (SHAPE, LFO, key tracking) + bias (SHIFT+SHAPE)
//   -> shaper (sine fold / triangle fold / soft saturation), anti-aliased
//      with first-order ADAA (antiderivative anti-aliasing)
//   -> DC blocker -> damping low-pass -> dry/wet -> soft clip

#pragma once

#include <stdint.h>

#if defined(__arm__)
#define WSH_INLINE static inline __attribute__((always_inline, optimize("Ofast")))
#else
#define WSH_INLINE static inline __attribute__((always_inline))
#include <cmath>
#endif

namespace wsh {

// ---------------------------------------------------------------------------
// Math helpers. Kept local so the firmware build never links libm.
// ---------------------------------------------------------------------------

WSH_INLINE float floorf_(float x) {
  const float t = (float)(int32_t)x;
  return (t > x) ? t - 1.f : t;
}

WSH_INLINE float fabsf_(float x) { return x < 0.f ? -x : x; }

WSH_INLINE float clampf_(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

WSH_INLINE float sqrtf_(float x) {
#if defined(__arm__)
  float r;
  __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
  return r;
#else
  return std::sqrt(x);
#endif
}

// sin(2*pi*t), t in turns, any range. Max error around 1e-7.
WSH_INLINE float sin_turns(float t) {
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

WSH_INLINE float cos_turns(float t) { return sin_turns(t + 0.25f); }

// Cubic soft clip, flat above |x| = 1.5.
WSH_INLINE float softclip(float x) {
  x = clampf_(x, -1.5f, 1.5f);
  return x - (4.f / 27.f) * x * x * x;
}

// ---------------------------------------------------------------------------
// Shapers. Each has f(u) and its antiderivative F(u) for ADAA.
// ---------------------------------------------------------------------------

enum ShaperType { kSineFold = 0, kTriFold = 1, kSaturate = 2, kNumShapers };

// Sine fold: f(u) = sin(pi/2 * u). Identity slope near 0, folds past |u| = 1.
WSH_INLINE float sine_f(float u) { return sin_turns(0.25f * u); }
WSH_INLINE float sine_F(float u) {
  return -0.636619772f * cos_turns(0.25f * u);  // -(2/pi) cos(pi/2 * u)
}

// Triangle fold: f(u) reflects u back into [-1, 1]. Period 4.
WSH_INLINE float tri_f(float u) {
  float w = u + 1.f;
  w -= 4.f * floorf_(w * 0.25f);  // [0, 4)
  return (w < 2.f) ? w - 1.f : 3.f - w;
}
WSH_INLINE float tri_F(float u) {
  float w = u + 1.f;
  w -= 4.f * floorf_(w * 0.25f);
  if (w < 2.f) {
    const float a = w - 1.f;
    return 0.5f * a * a - 0.5f;
  }
  const float b = 3.f - w;
  return 0.5f - 0.5f * b * b;
}

// Saturation: f(u) = u / sqrt(1 + u^2).
WSH_INLINE float sat_f(float u) { return u / sqrtf_(1.f + u * u); }
WSH_INLINE float sat_F(float u) { return sqrtf_(1.f + u * u); }

WSH_INLINE float shaper_f(int type, float u) {
  switch (type) {
    case kTriFold: return tri_f(u);
    case kSaturate: return sat_f(u);
    default: return sine_f(u);
  }
}

WSH_INLINE float shaper_F(int type, float u) {
  switch (type) {
    case kTriFold: return tri_F(u);
    case kSaturate: return sat_F(u);
    default: return sine_F(u);
  }
}

// ---------------------------------------------------------------------------
// Oscillator
// ---------------------------------------------------------------------------

struct Params {
  float shape = 0.f;       // SHAPE knob, [0, 1]: fold amount
  float shiftshape = 0.f;  // SHIFT+SHAPE, [0, 1]: bias (asymmetry)
  int type = kSineFold;    // param 1
  float source = 0.f;      // param 2, [0, 1]: sine -> triangle
  float sub = 0.f;         // param 3, [0, 1]: sub sine level
  float keytrack = 0.f;    // param 4, [0, 1]: less gain on high notes
  float damp = 0.f;        // param 5, [0, 1]: post low-pass amount
  float dry = 0.f;         // param 6, [0, 1]: dry/wet balance
  bool adaa = true;        // host-only switch, for A/B tests
};

class Oscillator {
 public:
  static constexpr float kMaxGain = 12.f;
  static constexpr float kAdaaEps = 1e-3f;

  Params params;

  void reset() {
    phase_ = 0.f;
    sub_phase_ = 0.f;
    u1_ = 0.f;
    F1_ = shaper_F(params.type, 0.f);
    x1_ = 0.f;
    dc_x1_ = 0.f;
    dc_y1_ = 0.f;
    lp_ = 0.f;
  }

  // Call when the shaper type changes, so ADAA state stays consistent.
  void onTypeChange() { F1_ = shaper_F(params.type, u1_); }

  // w0: phase increment in cycles per sample (f / 48000).
  // lfo: shape LFO value, roughly [-1, 1], added to SHAPE.
  void process(float *out, uint32_t frames, float w0, float lfo) {
    const Params &p = params;

    // Fold amount -> input gain. Squared for a smoother knob feel.
    const float amount = clampf_(p.shape + lfo, 0.f, 1.f);
    float gain = 1.f + (kMaxGain - 1.f) * amount * amount;
    // Key tracking: 0 at 60 Hz, gain halves around 1 kHz at full tracking.
    gain /= 1.f + p.keytrack * w0 * 50.f;
    const float bias = 2.f * p.shiftshape;

    // Ramp gain and bias over the block to avoid zipper noise.
    const float inv_frames = 1.f / (float)frames;
    const float gain_inc = (gain - gain_z_) * inv_frames;
    const float bias_inc = (bias - bias_z_) * inv_frames;
    float g = gain_z_;
    float b = bias_z_;

    const float src = p.source;
    const float sub = p.sub;
    const float src_norm = 1.f / (1.f + sub);
    const float lp_coef = 1.f - 0.97f * p.damp * (2.f - p.damp);
    const float wet = 1.f - p.dry;
    const float dry = p.dry;
    const int type = p.type;

    float phase = phase_;
    float sub_phase = sub_phase_;
    float u1 = u1_, F1 = F1_, x1 = x1_;
    float dc_x1 = dc_x1_, dc_y1 = dc_y1_, lp = lp_;

    for (uint32_t i = 0; i < frames; ++i) {
      // Source: sine <-> triangle, plus sub.
      const float s = sin_turns(phase);
      // Triangle in phase with the sine: 0 at phase 0, +1 at 0.25.
      float tp = phase + 0.75f;
      if (tp >= 1.f) tp -= 1.f;
      const float tri = 4.f * fabsf_(tp - 0.5f) - 1.f;
      float x = s + src * (tri - s);
      if (sub > 0.f) x = (x + sub * sin_turns(sub_phase)) * src_norm;

      // Shaper.
      g += gain_inc;
      b += bias_inc;
      const float u = g * x + b;
      float y;
      if (p.adaa) {
        const float F = shaper_F(type, u);
        const float d = u - u1;
        y = (fabsf_(d) > kAdaaEps) ? (F - F1) / d
                                   : shaper_f(type, 0.5f * (u + u1));
        u1 = u;
        F1 = F;
      } else {
        y = shaper_f(type, u);
      }

      // DC blocker (~5 Hz), needed once bias is applied.
      const float hp = y - dc_x1 + 0.99935f * dc_y1;
      dc_x1 = y;
      dc_y1 = hp;

      // Damping low-pass.
      lp += lp_coef * (hp - lp);

      // ADAA delays the wet path by half a sample; match it on the dry path.
      const float xd = p.adaa ? 0.5f * (x + x1) : x;
      x1 = x;

      out[i] = softclip(wet * lp + dry * xd);

      phase += w0;
      if (phase >= 1.f) phase -= 1.f;
      sub_phase += 0.5f * w0;
      if (sub_phase >= 1.f) sub_phase -= 1.f;
    }

    phase_ = phase;
    sub_phase_ = sub_phase;
    u1_ = u1;
    F1_ = F1;
    x1_ = x1;
    dc_x1_ = dc_x1;
    dc_y1_ = dc_y1;
    lp_ = lp;
    gain_z_ = gain;
    bias_z_ = bias;
  }

 private:
  float phase_ = 0.f;
  float sub_phase_ = 0.f;
  float u1_ = 0.f;
  float F1_ = -0.636619772f;
  float x1_ = 0.f;
  float dc_x1_ = 0.f;
  float dc_y1_ = 0.f;
  float lp_ = 0.f;
  float gain_z_ = 1.f;
  float bias_z_ = 0.f;
};

}  // namespace wsh
