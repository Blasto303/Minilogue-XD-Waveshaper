// Dungeon: metallic delay/reverb with a frequency shifter in the feedback
// loop. Inspired by the "dungeon" processor described in the Forge TME
// Vhikk X algorithm reference (reimplemented from that description, not
// from its code).
//
// Plain C++11, no logue SDK dependency: the same code runs on the
// minilogue xd and in the host renderer (tools/render).
//
// Per channel:
//   write = input + feedback (with some cross-feed from the other channel)
//   taps  = 6 reads from the delay line; tap 0 sets the echo time, the
//           others sit at irregular, seed-dependent fractions of it
//   raw   = FORM blends tap 0 alone (distinct echoes) with the signed sum of
//           all taps (dense, metallic), then 3 short allpass diffusers
//   shift = raw through the frequency shifter (CELL Hz)
//   FEED < 0: loop = shift                      (spiralling pitch)
//   FEED > 0: loop = (shift + raw) / sqrt(2)    (moving notches)
// All blends keep the power constant, so FEED alone sets the decay time.
//   feedback = soft clip(|FEED| * damped, high-passed loop)

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

// Fixed per unit variant: the part of the Vhikk control set that does not
// fit on the xd's 3 delay controls.
struct Voicing {
  float form;      // 0 = distinct echoes, 1 = dense diffuse cloud
  float cell_hz;   // frequency shift in the loop, signed
  float damp;      // loop low-pass, 1 = open
  uint32_t seed;   // tap spacing, diffuser lengths, modulation rates
};

class Dungeon {
 public:
  static constexpr float kRate = 48000.f;
  static constexpr float kMinTime = 0.01f;  // s, TIME fully left
  static constexpr float kMaxTime = 1.5f;   // s, TIME fully right
  // Above 1 on purpose: damping, the high-pass and the shifter all bleed
  // energy, so the loop only starts to hold on its own in the last ~15 % of
  // the FEED travel. There it drones indefinitely, held in check by the soft
  // clip inside the loop.
  static constexpr float kMaxFeed = 1.20f;

  void init(Memory *mem, const Voicing &v) {
    mem_ = mem;
    voicing_ = v;
    uint32_t s = v.seed * 2654435761u + 12345u;
    for (int ch = 0; ch < 2; ++ch) {
      // Tap 0 is the main echo. Others: irregular fractions, sorted so the
      // spacing never collapses, different on each side for width.
      ratio_[ch][0] = 1.f;
      for (int k = 1; k < kTaps; ++k) {
        const float lo = 0.12f + 0.15f * (float)(k - 1);
        ratio_[ch][k] = lo + 0.13f * rand01(s);
      }
      for (int k = 0; k < kTaps; ++k) {
        mod_rate_[ch][k] = 0.07f + 0.5f * rand01(s);  // Hz
        mod_phase_[ch][k] = rand01(s);
      }
      for (int d = 0; d < kDiffusers; ++d)
        ap_len_[ch][d] = 140 + (uint32_t)(760.f * rand01(s));
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
      lp_[ch] = hp_x1_[ch] = hp_y1_[ch] = 0.f;
      for (int k = 0; k < kTaps; ++k) tap_z_[ch][k] = -1.f;
    }
    pos_ = 0;
    ap_pos_ = 0;
    phase_ = 0.f;
  }

  // TIME, [0, 1]: main echo from 10 ms to 1.5 s, exponential.
  void setTime(float v) {
    v = clampf_(v, 0.f, 1.f);
    // kMinTime * (kMaxTime / kMinTime)^v, with exp2 done by squaring steps.
    time_s_ = kMinTime * pow150(v);
  }

  // DEPTH as FEED, [0, 1]: centre = no feedback. Left = feedback through the
  // shifter only; right = shifter mixed with the raw delay.
  void setFeed(float v) {
    float x = 2.f * clampf_(v, 0.f, 1.f) - 1.f;
    if (fabsf_(x) < 0.02f) x = 0.f;
    feed_ = x;
  }

  // SHIFT+DEPTH as MIX, [0, 1]: dry stays full up to 50 %, wet reaches full
  // at 50 %. Both full at the centre, like most delay pedals.
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

    // Per-block targets for each tap length (samples), with slow modulation
    // that grows with FORM (reverb-like movement).
    const float base = time_s_ * kRate;
    float tap_inc[2][kTaps];
    float tap[2][kTaps];
    const float mod_depth = form * (8.f + 0.004f * base);
    for (int ch = 0; ch < 2; ++ch) {
      for (int k = 0; k < kTaps; ++k) {
        mod_phase_[ch][k] += mod_rate_[ch][k] * (float)frames / kRate;
        mod_phase_[ch][k] -= floorf_(mod_phase_[ch][k]);
        float target = base * ratio_[ch][k] +
                       mod_depth * sin_turns(mod_phase_[ch][k]) + 2.f;
        target = clampf_(target, 2.f, (float)(kLineSize - 4));
        // Glide toward new lengths: turning TIME bends pitch like tape.
        // Slew is capped at half a sample per sample (an octave at most).
        float &z = tap_z_[ch][k];
        if (z < 0.f) z = target;
        const float max_step = 0.5f * (float)frames;
        const float next = z + clampf_(0.08f * (target - z), -max_step, max_step);
        tap[ch][k] = z;
        tap_inc[ch][k] = (next - z) * inv;
        z = next;
      }
    }

    const float feed = feed_;
    const float g = fabsf_(feed) * kMaxFeed;
    const float g_inc = (g - g_z_) * inv;
    float gz = g_z_;
    // 0 = shifter only (FEED left), 1 = shifter + raw (FEED right), with a
    // short crossfade around the centre instead of a hard switch.
    const float notch = clampf_((feed + 0.05f) * 10.f, 0.f, 1.f);
    // Equal-power blend of tap 0 and the dense tap sum.
    const float k_main = cos_turns(0.25f * form);
    const float k_dense = sin_turns(0.25f * form) * 0.40824829f;  // 1/sqrt(6)

    // Dense, high-feedback settings pile up energy: trim the wet level so
    // turning FEED up doesn't jump in volume.
    const float wet_t = (mix_ < 0.5f ? 2.f * mix_ : 1.f) / (1.f + 1.2f * form * g);
    const float dry_t = mix_ > 0.5f ? 2.f * (1.f - mix_) : 1.f;
    const float wet_inc = (wet_t - wet_z_) * inv;
    const float dry_inc = (dry_t - dry_z_) * inv;
    float wz = wet_z_, dz = dry_z_;

    const float w = vc.cell_hz / kRate;
    const float ap_g = 0.62f * form;
    const float cross = 0.2f + 0.3f * form;
    const float damp = vc.damp;

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

      float loop_out[2];
      float wet[2];
      for (int ch = 0; ch < 2; ++ch) {
        const float *line = mem_->line[ch];

        // Taps. Alternating signs keep the dense sum free of DC build-up.
        float main = 0.f, dense = 0.f;
        for (int k = 0; k < kTaps; ++k) {
          const float d = tap[ch][k];
          tap[ch][k] += tap_inc[ch][k];
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

        // Short allpass diffusers: metallic smear, stronger with FORM.
        for (int d = 0; d < kDiffusers; ++d) {
          float *ap = mem_->ap[ch][d];
          const float delayed = ap[(ap_pos - ap_len_[ch][d]) & kApMask];
          const float v = raw + ap_g * delayed;
          ap[ap_pos & kApMask] = v;
          raw = delayed - ap_g * v;
        }

        // Frequency shifter.
        float hi, hq;
        hil_[ch].process(raw, hi, hq);
        const float shifted = hi * cc[ch] + hq * ss[ch];
        const float proc =
            shifted + notch * (0.70710678f * (shifted + raw) - shifted);

        // Loop conditioning: high-pass ~25 Hz, damping low-pass.
        const float hp = proc - hp_x1_[ch] + 0.9967f * hp_y1_[ch];
        hp_x1_[ch] = proc;
        hp_y1_[ch] = hp;
        lp_[ch] += damp * (hp - lp_[ch]);
        loop_out[ch] = lp_[ch];
        wet[ch] = proc;
      }

      for (int ch = 0; ch < 2; ++ch) {
        const float x = io[2 * n + ch];
        const float fb = (1.f - cross) * loop_out[ch] + cross * loop_out[1 - ch];
        // Soft clip inside the loop: high FEED saturates instead of running
        // away.
        mem_->line[ch][pos] = x + softclip(1.2f * gz * fb) * (1.f / 1.2f);
        io[2 * n + ch] = softclip(dz * x + wz * wet[ch]);
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

  static float rand01(uint32_t &s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (float)(s >> 8) * (1.f / 16777216.f);
  }

  // 150^v for v in [0, 1] without libm: 150^v = 2^(v * log2(150)).
  static float pow150(float v) {
    const float e = v * 7.228819f;
    const int32_t ip = (int32_t)e;
    const float f = e - (float)ip;
    // 2^f on [0, 1), cubic fit, max error ~1e-4.
    const float p = 1.f + f * (0.6951786f + f * (0.2261929f + f * 0.0786281f));
    return (float)(1u << ip) * p;
  }

  Memory *mem_ = nullptr;
  Voicing voicing_ = {0.f, 0.f, 1.f, 1};
  float ratio_[2][kTaps];
  float mod_rate_[2][kTaps];
  float mod_phase_[2][kTaps];
  uint32_t ap_len_[2][kDiffusers];
  float tap_z_[2][kTaps];
  fshift::Hilbert hil_[2];
  float lp_[2], hp_x1_[2], hp_y1_[2];
  uint32_t pos_ = 0, ap_pos_ = 0;
  uint32_t clear_pos_ = 0;
  float phase_ = 0.f;
  float time_s_ = 0.12f, feed_ = 0.f, mix_ = 0.5f;
  float g_z_ = 0.f, wet_z_ = 1.f, dry_z_ = 1.f;
};

}  // namespace dungeon
