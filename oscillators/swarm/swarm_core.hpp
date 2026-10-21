// Swarm: 4-oscillator cluster generator. DSP core.
//
// Inspired by the generator section of the Forge TME Vhikk X (cluster
// scanning, WARP / SPAN / FIELD / FUSE / SCAN / SEED), reimplemented from
// its public description, not from its code.
//
// Plain C++11, no logue SDK dependency: the same code runs on the
// minilogue xd and in the host renderer (tools/render).
//
// A cluster is 8 points. Each point gives each of the 4 oscillators a pitch
// offset (from SEED, scaled by SPAN, shaped by ALGO) and a waveform (from
// SCAN). MORPH scans the points with crossfades: pitches glide, waveforms
// blend. Oscillator 0 always stays on the played note.
//
// Per sample:
//   each oscillator is modulated by the previous one (oscillator 0 by
//   oscillator 3, one sample late): WARP left = phase modulation,
//   WARP right = exponential frequency modulation
//   mix -> FUSE (left: noise jitters the frequencies, right: wavefolder)
//       -> FIELD (comb filter tuned to the note: left hollow, right resonant)
//       -> soft clip

#pragma once

#include <stdint.h>

#include "../../common/dsp_math.hpp"

namespace swarm {

using namespace dspm;

static const int kOsc = 4;
static const int kPoints = 8;
static const int kWaves = 8;
static const int kTableSize = 256;
static const int kHarmonics = 32;
static const uint32_t kCombSize = 2048;  // longest comb: 42 ms (~24 Hz)
static const uint32_t kCombMask = kCombSize - 1;

enum Algo { kFree = 0, kSemitone = 1, kMinor = 2, kHarmonic = 3, kNumAlgos };

struct Params {
  float morph = 0.f;   // SHAPE, [0, 1]
  float warp = 0.5f;   // SHIFT+SHAPE, [0, 1], 0.5 = neutral
  float span = 0.f;    // [0, 1]
  float field = 0.f;   // [-1, 1]
  float fuse = 0.f;    // [-1, 1]
  int scan = 0;        // [0, 100], waveform set
  int seed = 0;        // [0, 100], pitch offset set
  int algo = kFree;
};

// How far each oscillator's pitch moves from point to point, around its
// own fixed base pitch (1 = fully independent points). Phase offsets move
// instead, so MORPH is heard more as phasing than as pitch jumps.
static const float kPitchTravel = 0.15f;
static const float kPhaseTravel = 0.5f;  // turns, at most half a period

class Swarm {
 public:
  Params params;

  // Builds the 8 band-limited single-cycle tables (additive, 32 harmonics).
  void init() {
    for (int w = 0; w < kWaves; ++w) {
      float peak = 0.f;
      for (int i = 0; i < kTableSize; ++i) {
        const float t = (float)i / (float)kTableSize;
        float y = 0.f;
        for (int h = 1; h <= kHarmonics; ++h) {
          const float a = harmonic(w, h);
          if (a != 0.f) y += a * sin_turns(t * (float)h);
        }
        table_[w][i] = y;
        peak = fabsf_(y) > peak ? fabsf_(y) : peak;
      }
      const float norm = peak > 0.f ? 1.f / peak : 1.f;
      for (int i = 0; i < kTableSize; ++i) table_[w][i] *= norm;
      table_[w][kTableSize] = table_[w][0];
    }
    for (uint32_t i = 0; i < kCombSize; ++i) comb_[i] = 0.f;
    for (int j = 0; j < kOsc; ++j) phase_[j] = 0.25f * (float)j;
    last_seed_ = -1;
    last_scan_ = -1;
  }

  // w0: phase increment of the played note, in cycles per sample.
  // lfo: SHAPE LFO, added to MORPH.
  void process(float *out, uint32_t frames, float w0, float lfo) {
    const Params &p = params;
    if (p.seed != last_seed_) buildPitches(p.seed);
    if (p.scan != last_scan_) buildWaves(p.scan);

    // --- Per block: the two cluster points around MORPH. --------------------
    const float m = clampf_(p.morph + lfo, 0.f, 1.f) * (float)(kPoints - 1);
    const int i0 = (int)m < kPoints - 1 ? (int)m : kPoints - 2;
    const float t = m - (float)i0;  // crossfade toward point i0 + 1

    float w_target[kOsc];
    const float *wave_a[kOsc];
    const float *wave_b[kOsc];
    float off_a[kOsc], off_b[kOsc];
    for (int j = 0; j < kOsc; ++j) {
      const float s = (1.f - t) * semis(i0, j) + t * semis(i0 + 1, j);
      w_target[j] = w0 * exp2f_(s * (1.f / 12.f));
      wave_a[j] = table_[wave_[i0][j]];
      wave_b[j] = table_[wave_[i0 + 1][j]];
      off_a[j] = phase_off_[i0][j];
      off_b[j] = phase_off_[i0 + 1][j];
    }

    const float inv = 1.f / (float)frames;
    float w_inc[kOsc], w[kOsc];
    for (int j = 0; j < kOsc; ++j) {
      if (w_z_[j] <= 0.f) w_z_[j] = w_target[j];
      w[j] = w_z_[j];
      w_inc[j] = (w_target[j] - w_z_[j]) * inv;
      w_z_[j] = w_target[j];
    }
    // Ramp the crossfade within a pair of points; when MORPH moves to a new
    // pair, start from the new position (the waveforms already line up there).
    float tz = (t_z_ < 0.f || i0 != i0_z_) ? t : t_z_;
    const float t_inc = (t - tz) * inv;
    t_z_ = t;
    i0_z_ = i0;

    const float wx = 2.f * p.warp - 1.f;
    const float pm = wx < 0.f ? -wx * 0.6f : 0.f;   // turns
    const float fm = wx > 0.f ? wx * 3.f : 0.f;     // octaves

    const float fuse = p.fuse;
    const float jitter = fuse < 0.f ? -fuse * 0.15f : 0.f;
    const float fold = fuse > 0.f ? fuse : 0.f;
    const float fold_gain = 1.f + 5.f * fold;

    // FIELD: one knob sets both the comb tuning (fundamental up to 2 octaves
    // above) and its feedback, like the Vhikk's interwoven parameters.
    const float field = p.field;
    const float fa = fabsf_(field);
    // Fades in over the first 10 % of travel either side, so the centre is a
    // clean bypass without a jump.
    const float comb_amt = clampf_(fa * 10.f, 0.f, 1.f);
    const float comb_fb = (field < 0.f ? -1.f : 1.f) * (0.5f + 0.45f * fa) * comb_amt;
    const float comb_norm = 1.f - 0.5f * fabsf_(comb_fb);
    const float delay_target = clampf_(1.f / (w0 * (1.f + 3.f * fa)), 2.f, (float)(kCombSize - 2));
    if (comb_delay_z_ <= 0.f) comb_delay_z_ = delay_target;
    float comb_delay = comb_delay_z_;
    const float delay_inc = (delay_target - comb_delay_z_) * inv;
    comb_delay_z_ = delay_target;

    float y_prev = y_last_;
    for (uint32_t n = 0; n < frames; ++n) {
      tz += t_inc;
      // Low-passed noise for FUSE jitter (~150 Hz).
      if (jitter > 0.f) noise_lp_ += 0.02f * (rand11() - noise_lp_);

      float mix = 0.f;
      float mod = y_prev;  // oscillator 3's last output modulates oscillator 0
      for (int j = 0; j < kOsc; ++j) {
        float wj = w[j];
        w[j] += w_inc[j];
        if (fm > 0.f) wj *= exp2f_(fm * mod);
        if (jitter > 0.f) wj *= 1.f + jitter * noise_lp_ * (j & 1 ? -1.f : 1.f) * (1.f + 0.3f * (float)j);

        float ph = phase_[j] + off_a[j] + tz * (off_b[j] - off_a[j]) + pm * mod;
        ph -= floorf_(ph);
        const float idx = ph * (float)kTableSize;
        const int k = (int)idx;
        const float fr = idx - (float)k;
        const float a = wave_a[j][k] + fr * (wave_a[j][k + 1] - wave_a[j][k]);
        const float b = wave_b[j][k] + fr * (wave_b[j][k + 1] - wave_b[j][k]);
        const float y = a + tz * (b - a);

        phase_[j] += wj;
        phase_[j] -= floorf_(phase_[j]);
        mix += y;
        mod = y;
      }
      y_prev = mod;
      mix *= 0.3f;

      if (fold > 0.f) mix += fold * (sin_turns(0.25f * fold_gain * mix) - mix);

      comb_delay += delay_inc;
      {
        const float rp = (float)comb_pos_ - comb_delay + (float)kCombSize;
        const uint32_t ci = (uint32_t)rp;
        const float cf = rp - (float)ci;
        const float d0 = comb_[ci & kCombMask];
        const float d1 = comb_[(ci + 1) & kCombMask];
        const float yc = mix + comb_fb * (d0 + cf * (d1 - d0));
        comb_[comb_pos_] = yc;
        comb_pos_ = (comb_pos_ + 1) & kCombMask;
        mix += comb_amt * (yc * comb_norm - mix);
      }

      out[n] = softclip(mix) * 0.9f;
    }
    y_last_ = y_prev;
  }

 private:
  // Harmonic amplitudes of the 8 waveforms.
  static float harmonic(int w, int h) {
    const float fh = (float)h;
    switch (w) {
      case 0: return h == 1 ? 1.f : 0.f;                                   // sine
      case 1: return (h & 1) ? ((h & 2) ? -1.f : 1.f) / (fh * fh) : 0.f;  // triangle
      case 2: return 1.f / fh;                                             // saw
      case 3: return (h & 1) ? 1.f / fh : 0.f;                             // square
      case 4: return sin_turns(0.125f * fh) / fh;                         // pulse 25 %
      case 5: {                                                            // formant
        const float d = (fh - 6.f) * 0.5f;
        return 1.f / (1.f + d * d * d * d) + (h == 1 ? 0.4f : 0.f);
      }
      case 6:                                                              // organ
        switch (h) {
          case 1: return 1.f;
          case 2: return 0.5f;
          case 4: return 0.33f;
          case 8: return 0.25f;
          case 16: return 0.2f;
          default: return 0.f;
        }
      default: return h == 1 ? 0.3f : ((h & 1) ? 1.f / (0.5f * fh + 1.f) : 0.f);  // hollow
    }
  }

  // Pitch offset of oscillator j at point i, in semitones.
  float semis(int i, int j) const {
    if (j == 0) return 0.f;
    const float b = pitch_[i][j];  // [-1, 1]
    const float span = params.span;
    switch (params.algo) {
      case kSemitone: return (float)(int32_t)(span * 12.f * b + (b < 0.f ? -0.5f : 0.5f));
      case kMinor: return quantizeMinor(span * 12.f * b);
      case kHarmonic: {
        // Harmonics 1..8 above the note, more of them with SPAN.
        static const float kHarmSemis[8] = {0.f, 12.f, 19.0196f, 24.f, 27.8631f, 31.0196f, 33.6883f, 36.f};
        int h = (int)(fabsf_(b) * (1.f + 7.f * span));
        if (h > 7) h = 7;
        return kHarmSemis[h];
      }
      default: return span * 12.f * b + detune_[i][j];
    }
  }

  static float quantizeMinor(float s) {
    static const float kScale[8] = {0.f, 2.f, 3.f, 5.f, 7.f, 8.f, 10.f, 12.f};
    const float oct = floorf_(s * (1.f / 12.f));
    const float r = s - 12.f * oct;
    float best = 0.f, dist = 99.f;
    for (int k = 0; k < 8; ++k) {
      const float d = fabsf_(r - kScale[k]);
      if (d < dist) {
        dist = d;
        best = kScale[k];
      }
    }
    return 12.f * oct + best;
  }

  void buildPitches(int seed) {
    rng_ = 0x9E3779B9u * (uint32_t)(seed + 1);
    for (int i = 0; i < kPoints; ++i)
      for (int j = 0; j < kOsc; ++j) {
        pitch_[i][j] = rand11();
        detune_[i][j] = 0.12f * rand11();  // free mode: a little beating
      }
    // Pull each oscillator's points toward its average pitch.
    for (int j = 0; j < kOsc; ++j) {
      float base = 0.f;
      for (int i = 0; i < kPoints; ++i) base += pitch_[i][j];
      base *= 1.f / (float)kPoints;
      for (int i = 0; i < kPoints; ++i)
        pitch_[i][j] = base + pitch_travel_ * (pitch_[i][j] - base);
    }
    // Phase offsets, drawn after the pitches so a given SEED keeps its chord.
    for (int i = 0; i < kPoints; ++i)
      for (int j = 0; j < kOsc; ++j)
        phase_off_[i][j] = (j == 0) ? 0.f : kPhaseTravel * rand11();
    last_seed_ = seed;
  }

  void buildWaves(int scan) {
    rng_ = 0x85EBCA6Bu * (uint32_t)(scan + 1);
    for (int i = 0; i < kPoints; ++i)
      for (int j = 0; j < kOsc; ++j) wave_[i][j] = (uint8_t)(rand01() * (float)kWaves) & 7;
    last_scan_ = scan;
  }

  float rand01() {
    uint32_t s = rng_;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    rng_ = s;
    return (float)(s >> 8) * (1.f / 16777216.f);
  }
  float rand11() { return 2.f * rand01() - 1.f; }

  float table_[kWaves][kTableSize + 1];
  float pitch_[kPoints][kOsc];
  float detune_[kPoints][kOsc];
  float phase_off_[kPoints][kOsc];
  uint8_t wave_[kPoints][kOsc];
  float comb_[kCombSize];
  float phase_[kOsc];
  float w_z_[kOsc] = {0.f, 0.f, 0.f, 0.f};
  float t_z_ = -1.f;
  int i0_z_ = -1;
  float comb_delay_z_ = 0.f;
  float y_last_ = 0.f;
  float noise_lp_ = 0.f;
  uint32_t comb_pos_ = 0;
  uint32_t rng_ = 1;
  int last_seed_ = -1, last_scan_ = -1;

 public:
  // Host-side tuning only (A/B renders); the firmware keeps the default.
  void setPitchTravel(float v) {
    pitch_travel_ = v;
    last_seed_ = -1;
  }

 private:
  float pitch_travel_ = kPitchTravel;
};

}  // namespace swarm
