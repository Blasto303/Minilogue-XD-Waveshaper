// Dungeon: chaotic metallic delay with a frequency shifter, distortion and
// a resonant bass filter inside the feedback loop. Inspired by the
// "dungeon" processor described in the Forge TME Vhikk X algorithm reference
// (reimplemented from that description, not from its code).
//
// Plain C++11, no logue SDK dependency: the same code runs on the
// minilogue xd and in the host renderer (tools/render).
//
// Per channel, per sample:
//   write = input + feedback + a whisper of noise (so the loop can start
//           oscillating from silence)
//   taps  = 6 reads at irregular fractions of TIME. Read positions move
//           with a Lorenz attractor, jump at random (and glide there), and
//           are modulated at audio rate by the loop's own output (FM growl)
//   raw   = FORM blends tap 0 with the signed sum of all taps, then 3 short
//           allpass diffusers
//   shift = frequency shifter (CELL Hz, wobbled by the attractor). Downward
//           shifts fold below 0 Hz into sub rumble
//   FEED < 0: loop = shift                    spirals, more FM
//   FEED > 0: loop = (shift + raw) / sqrt(2)  moving notches, more growl
//   loop -> resonant band-pass growl (cutoff driven by the attractor)
//        -> asymmetric saturation, drive rising with |FEED|
//        -> optional sample-rate reduction
//        -> damping, DC blocker
// Past a voicing-dependent point of the FEED travel the loop gain passes 1:
// from there it self-oscillates, bounded only by the saturation.

#pragma once

#include <stdint.h>

#include "dsp_math.hpp"
#include "freqshift_core.hpp"

namespace dungeon {

using namespace dspm;

static const int kTaps = 6;
static const int kDiffusers = 3;
static const uint32_t kLineSize = 1u << 17;  // 2.73 s per channel
static const uint32_t kLineMask = kLineSize - 1;
static const uint32_t kApSize = 1024;        // 21 ms per diffuser
static const uint32_t kApMask = kApSize - 1;

// Buffers live outside the class so the firmware can put them in SDRAM.
struct Memory {
  float line[2][kLineSize];
  float ap[2][kDiffusers][kApSize];
};

// Fixed per unit variant: what does not fit on the xd's 3 delay controls.
struct Voicing {
  float form;       // 0 = tap 0 only, 1 = dense signed tap sum
  float cell_hz;    // frequency shift in the loop, signed
  float damp;       // loop low-pass, 1 = open
  uint32_t seed;    // tap spacing, diffuser lengths
  float drive;      // saturation drive at full FEED
  float growl_hz;   // resonant band-pass centre in the loop
  float growl_q;
  float fm;         // audio-rate read modulation at full FEED, in samples
  float chaos;      // attractor speed and depth, 0..1
  uint32_t crush;   // sample-and-hold factor in the loop, 1 = off
  float max_feed;   // loop gain at full FEED; just above 1 keeps the
                    // self-oscillation fragile, higher pins it to the clip
};

class Dungeon {
 public:
  static constexpr float kRate = 48000.f;
  static constexpr float kMinTime = 0.01f;  // s, TIME fully left
  static constexpr float kMaxTime = 1.5f;   // s, TIME fully right

  void init(Memory *mem, const Voicing &v) {
    mem_ = mem;
    voicing_ = v;
    rng_ = v.seed * 2654435761u + 12345u;
    for (int ch = 0; ch < 2; ++ch) {
      ratio_[ch][0] = 1.f;
      for (int k = 1; k < kTaps; ++k) ratio_[ch][k] = randomRatio(k);
      for (int d = 0; d < kDiffusers; ++d)
        ap_len_[ch][d] = 140 + (uint32_t)(760.f * rand01());
    }
    reset();
  }

  // Clears everything at once. Fine at init, too slow for the audio thread.
  void reset() {
    resetState();
    clear_pos_ = 0;
    clearChunk(kLineSize);
  }

  // For resume: the delay lines are cleared over the next blocks while the
  // wet path stays silent, so the audio thread never stalls.
  void resetSoft() {
    resetState();
    clear_pos_ = 0;
  }

  void resetState() {
    for (int ch = 0; ch < 2; ++ch) {
      for (int d = 0; d < kDiffusers; ++d)
        for (uint32_t i = 0; i < kApSize; ++i) mem_->ap[ch][d][i] = 0.f;
      hil_[ch].reset();
      lp_[ch] = dc_x1_[ch] = dc_y1_[ch] = 0.f;
      sv1_[ch] = sv2_[ch] = 0.f;
      prev_[ch] = hold_[ch] = 0.f;
      for (int k = 0; k < kTaps; ++k) tap_z_[ch][k] = -1.f;
    }
    lx_ = 0.1f;
    ly_ = 0.f;
    lz_ = 20.f;
    pos_ = 0;
    ap_pos_ = 0;
    phase_ = 0.f;
    crush_n_ = 0;
  }

  // TIME, [0, 1]: main echo from 10 ms to 1.5 s, exponential.
  void setTime(float v) { time_s_ = kMinTime * pow150(clampf_(v, 0.f, 1.f)); }

  // DEPTH as FEED, [0, 1]: centre = no feedback.
  void setFeed(float v) {
    float x = 2.f * clampf_(v, 0.f, 1.f) - 1.f;
    if (fabsf_(x) < 0.02f) x = 0.f;
    feed_ = x;
  }

  // SHIFT+DEPTH as MIX, [0, 1]: both at full level at the centre.
  void setMix(float v) { mix_ = clampf_(v, 0.f, 1.f); }

  // io: stereo interleaved, processed in place.
  void process(float *io, uint32_t frames) {
    if (clear_pos_ < kLineSize) {
      clearChunk(kClearPerBlock);
      return;  // dry passes through untouched
    }
    const float inv = 1.f / (float)frames;
    const Voicing &vc = voicing_;
    const float form = vc.form;
    const float feed = feed_;
    const float amt = fabsf_(feed);
    const float left = feed < 0.f ? amt : 0.f;
    const float right = feed > 0.f ? amt : 0.f;

    // --- Chaos: Lorenz attractor, one step per block. -----------------------
    const float dt = 0.0025f + 0.009f * vc.chaos * (0.4f + amt);
    const float lzx = 10.f * (ly_ - lx_);
    const float lzy = lx_ * (28.f - lz_) - ly_;
    const float lzz = lx_ * ly_ - 2.6666667f * lz_;
    lx_ += dt * lzx;
    ly_ += dt * lzy;
    lz_ += dt * lzz;
    const float c1 = clampf_(lx_ * 0.05f, -1.f, 1.f);
    const float c2 = clampf_(ly_ * 0.037f, -1.f, 1.f);
    const float c3 = clampf_((lz_ - 25.f) * 0.04f, -1.f, 1.f);
    const float ch_depth = vc.chaos * (0.25f + amt);

    // Random tap jumps: more often with more chaos and FEED.
    if (rand01() < vc.chaos * amt * 0.004f) {
      const int ch = (int)(rand01() * 2.f) & 1;
      int k = 1 + (int)(rand01() * (float)(kTaps - 1));
      if (k >= kTaps) k = kTaps - 1;
      ratio_[ch][k] = randomRatio(k);
    }

    // --- Tap lengths (samples), gliding toward their targets. ---------------
    const float base = time_s_ * kRate;
    float tap_inc[2][kTaps];
    float tap[2][kTaps];
    for (int ch = 0; ch < 2; ++ch) {
      const float cm = ch ? c2 : c1;
      for (int k = 0; k < kTaps; ++k) {
        const float wob = 1.f + 0.06f * ch_depth * cm * (k & 1 ? -1.f : 1.f);
        float target = base * ratio_[ch][k] * wob + 4.f;
        target = clampf_(target, 4.f, (float)(kLineSize - 512));
        float &z = tap_z_[ch][k];
        if (z < 0.f) z = target;
        // Glide: capped at half a sample per sample (an octave of bend).
        const float max_step = 0.5f * (float)frames;
        const float next = z + clampf_(0.05f * (target - z), -max_step, max_step);
        tap[ch][k] = z;
        tap_inc[ch][k] = (next - z) * inv;
        z = next;
      }
    }

    // --- Per-block coefficients. --------------------------------------------
    // Loop gain: x^0.8 curve.
    // The attractor pushes the gain around 1, so a self-oscillating loop
    // swells, chokes and restarts on its own.
    const float g = vc.max_feed * (amt > 0.f ? powf_approx(amt, 0.8f) : 0.f) *
                    (1.f + 0.6f * ch_depth * c3);
    const float g_inc = (g - g_z_) * inv;
    float gz = g_z_;

    const float notch = clampf_((feed + 0.05f) * 10.f, 0.f, 1.f);
    const float k_main = cos_turns(0.25f * form);
    const float k_dense = sin_turns(0.25f * form) * 0.40824829f;  // 1/sqrt(6)

    const float drive = 1.f + (vc.drive - 1.f) * amt * amt;
    const float bias = 0.35f * amt;
    const float sat_off = softclip(bias);
    const float fm = vc.fm * (0.15f + 0.85f * left + 0.3f * right);
    const float growl_mix = 0.6f * (0.2f + right + 0.4f * left);

    // Growl filter (TPT state-variable), cutoff moved by the attractor.
    const float fc = clampf_(vc.growl_hz * (1.f + 0.6f * ch_depth * c3), 20.f, 2000.f);
    const float wg = 3.14159265f * fc / kRate;
    const float sg = wg * (1.f + wg * wg * 0.33333f);  // tan(wg), small angle
    const float sk = 1.f / vc.growl_q;
    const float a1 = 1.f / (1.f + sg * (sg + sk));
    const float a2 = sg * a1;
    const float a3 = sg * a2;
    const float growl_gain = vc.growl_q * 0.5f;

    const float shift_hz = vc.cell_hz * (1.f + 0.5f * ch_depth * c1);
    const float w = shift_hz / kRate;

    const float wet_t = mix_ < 0.5f ? 2.f * mix_ : 1.f;
    const float dry_t = mix_ > 0.5f ? 2.f * (1.f - mix_) : 1.f;
    const float wet_inc = (wet_t - wet_z_) * inv;
    const float dry_inc = (dry_t - dry_z_) * inv;
    float wz = wet_z_, dz = dry_z_;

    const float ap_g = 0.62f * form;
    const float cross = 0.2f + 0.3f * form;
    const float damp = vc.damp;
    const uint32_t crush = vc.crush ? vc.crush : 1;

    float phase = phase_;
    uint32_t pos = pos_;
    uint32_t ap_pos = ap_pos_;

    for (uint32_t n = 0; n < frames; ++n) {
      gz += g_inc;
      wz += wet_inc;
      dz += dry_inc;

      const float c = cos_turns(phase);
      const float s = sin_turns(phase);
      const float cc[2] = {c, -s};
      const float ss[2] = {s, c};

      const bool hold = (crush_n_ != 0);
      if (++crush_n_ >= crush) crush_n_ = 0;

      float loop_out[2];
      for (int ch = 0; ch < 2; ++ch) {
        const float *line = mem_->line[ch];
        // Audio-rate read modulation from the loop's own last output.
        const float fm_off = fm * prev_[ch];

        float main = 0.f, dense = 0.f;
        for (int k = 0; k < kTaps; ++k) {
          float d = tap[ch][k] + (k & 1 ? -fm_off : fm_off);
          tap[ch][k] += tap_inc[ch][k];
          if (d < 2.f) d = 2.f;
          const float rp = (float)pos - d + (float)kLineSize;
          const uint32_t i0 = (uint32_t)rp;
          const float fr = rp - (float)i0;
          const float a = line[i0 & kLineMask];
          const float b = line[(i0 + 1) & kLineMask];
          const float y = a + fr * (b - a);
          if (k == 0) main = y;
          dense += (k & 1) ? -y : y;
        }
        float raw = k_main * main + k_dense * dense;

        for (int d = 0; d < kDiffusers; ++d) {
          float *ap = mem_->ap[ch][d];
          const float delayed = ap[(ap_pos - ap_len_[ch][d]) & kApMask];
          const float v = raw + ap_g * delayed;
          ap[ap_pos & kApMask] = v;
          raw = delayed - ap_g * v;
        }

        float hi, hq;
        hil_[ch].process(raw, hi, hq);
        const float shifted = hi * cc[ch] + hq * ss[ch];
        float proc = shifted + notch * (0.70710678f * (shifted + raw) - shifted);

        // Resonant growl band-pass, added on top.
        const float v3 = proc - sv2_[ch];
        const float v1 = a1 * sv1_[ch] + a2 * v3;
        const float v2 = sv2_[ch] + a2 * sv1_[ch] + a3 * v3;
        sv1_[ch] = 2.f * v1 - sv1_[ch];
        sv2_[ch] = 2.f * v2 - sv2_[ch];
        proc += growl_mix * growl_gain * v1;

        // Asymmetric saturation (even harmonics, grind).
        float sat = softclip(drive * proc + bias) - sat_off;

        // Sample-rate reduction.
        if (hold) sat = hold_[ch];
        hold_[ch] = sat;

        // Damping, then DC blocker (~5 Hz: keeps the sub, removes the offset
        // the asymmetric saturation creates).
        lp_[ch] += damp * (sat - lp_[ch]);
        const float dc = lp_[ch] - dc_x1_[ch] + 0.99935f * dc_y1_[ch];
        dc_x1_[ch] = lp_[ch];
        dc_y1_[ch] = dc;

        loop_out[ch] = dc;
        prev_[ch] = dc;
      }

      for (int ch = 0; ch < 2; ++ch) {
        const float x = io[2 * n + ch];
        const float fb = (1.f - cross) * loop_out[ch] + cross * loop_out[1 - ch];
        const float noise = 2e-5f * (rand01() - 0.5f);
        mem_->line[ch][pos] = x + gz * fb + noise;
        // The saturated loop sits near full scale: trim it to the dry level.
        io[2 * n + ch] = softclip(dz * x + 0.3f * wz * loop_out[ch]);
      }

      pos = (pos + 1) & kLineMask;
      ap_pos = (ap_pos + 1) & kApMask;
      phase += w;
      phase -= floorf_(phase);
    }

    phase_ = phase;
    pos_ = pos;
    ap_pos_ = ap_pos;
    g_z_ = g;
    wet_z_ = wet_t;
    dry_z_ = dry_t;
  }

 private:
  static const uint32_t kClearPerBlock = 8192;  // 16 blocks for a full clear

  void clearChunk(uint32_t n) {
    const uint32_t end = (clear_pos_ + n > kLineSize) ? kLineSize : clear_pos_ + n;
    for (int ch = 0; ch < 2; ++ch)
      for (uint32_t i = clear_pos_; i < end; ++i) mem_->line[ch][i] = 0.f;
    clear_pos_ = end;
  }

  float rand01() {
    uint32_t s = rng_;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    rng_ = s;
    return (float)(s >> 8) * (1.f / 16777216.f);
  }

  // Irregular tap fraction for slot k (1..5), spread so taps never collapse.
  float randomRatio(int k) {
    const float lo = 0.12f + 0.15f * (float)(k - 1);
    return lo + 0.13f * rand01();
  }

  // 2^e for e in [0, 8), cubic fit for the fraction (max error ~1e-4).
  static float exp2_pos(float e) {
    const int32_t ip = (int32_t)e;
    const float f = e - (float)ip;
    const float p = 1.f + f * (0.6951786f + f * (0.2261929f + f * 0.0786281f));
    return (float)(1u << ip) * p;
  }

  // 150^v for v in [0, 1]: 2^(v * log2(150)).
  static float pow150(float v) { return exp2_pos(v * 7.228819f); }

  // x^p for x in (0, 1], p > 0, via 2^(p * log2(x)). Approximate, fine for
  // shaping a knob curve.
  static float powf_approx(float x, float p) {
    union {
      float f;
      uint32_t i;
    } u;
    u.f = x;
    const float e = (float)((int32_t)((u.i >> 23) & 0xff) - 127);
    u.i = (u.i & 0x007fffffu) | 0x3f800000u;  // mantissa in [1, 2)
    const float m = u.f;
    // log2(m) on [1, 2), quadratic exact at both ends (max error ~0.008).
    const float t = m - 1.f;
    const float l2 = e + t * (1.3426715f - 0.3426715f * t);
    const float y = -p * l2;  // >= 0
    return 1.f / exp2_pos(y < 7.9f ? y : 7.9f);
  }

  Memory *mem_ = nullptr;
  Voicing voicing_ = {0.f, 0.f, 1.f, 1, 1.f, 100.f, 1.f, 0.f, 0.f, 1, 1.f};
  uint32_t rng_ = 1;
  float ratio_[2][kTaps];
  uint32_t ap_len_[2][kDiffusers];
  float tap_z_[2][kTaps];
  fshift::Hilbert hil_[2];
  float lp_[2], dc_x1_[2], dc_y1_[2];
  float sv1_[2], sv2_[2];
  float prev_[2], hold_[2];
  float lx_ = 0.1f, ly_ = 0.f, lz_ = 20.f;
  uint32_t pos_ = 0, ap_pos_ = 0;
  uint32_t clear_pos_ = 0;
  uint32_t crush_n_ = 0;
  float phase_ = 0.f;
  float time_s_ = 0.12f, feed_ = 0.f, mix_ = 0.5f;
  float g_z_ = 0.f, wet_z_ = 1.f, dry_z_ = 1.f;
};

}  // namespace dungeon
