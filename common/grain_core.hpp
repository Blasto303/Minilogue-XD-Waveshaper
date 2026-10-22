// Grain: compact granular processor in the spirit of the granular mode of
// Mutable Instruments Clouds (written from scratch, no code from Clouds).
//
// Plain C++11, no logue SDK dependency: the same code runs on the
// minilogue xd and in the host renderer (tools/render).
//
// The input is recorded continuously into ~5.5 s of stereo memory. Grains
// read short windowed pieces of it:
//   TIME   = POSITION: how far back the grains read (5 ms to ~5 s).
//            Fully right = FREEZE: recording stops and grains are scattered
//            over the whole frozen memory.
//   DEPTH  = DENSITY, bipolar: centre = no new grains; left = regular
//            grains, denser further left; right = random grains, denser
//            further right.
//   SHIFT+DEPTH = dry/wet.
// Grain size, pitch set, window shape, stereo spread, feedback and
// diffusion are fixed per variant.

#pragma once

#include <stdint.h>

#include "dsp_math.hpp"

namespace grain {

using namespace dspm;

static const uint32_t kBufSize = 1u << 18;  // 5.46 s per channel
static const uint32_t kBufMask = kBufSize - 1;
static const int kMaxGrains = 16;
static const int kWinSize = 256;
static const int kDiffusers = 2;
static const uint32_t kApSize = 2048;
static const uint32_t kApMask = kApSize - 1;

// Buffers live outside the class so the firmware can put them in SDRAM.
struct Memory {
  float buf[2][kBufSize];
  float ap[2][kDiffusers][kApSize];
};

struct Voicing {
  float size_s;        // average grain length, seconds
  float size_rand;     // 0..1, random length variation
  float pitches[4];    // semitones; each grain picks one at random
  int num_pitches;     // 1..4
  float detune_cents;  // random detune per grain, +/-
  float texture;       // window: 0 = smooth (Hann), 1 = almost square
  float spread;        // stereo spread of the grains, 0..1
  float feedback;      // wet written back into memory (not when frozen)
  float diffuse;       // allpass smear on the wet signal, 0..1
  uint32_t seed;
};

class Grains {
 public:
  static constexpr float kRate = 48000.f;

  void init(Memory *mem, const Voicing &v) {
    mem_ = mem;
    voicing_ = v;
    rng_ = v.seed * 2654435761u + 7u;
    // Window table: a Hann window pushed toward a flat top by TEXTURE.
    const float k = 1.f + 8.f * v.texture * v.texture;
    for (int i = 0; i <= kWinSize; ++i) {
      const float h = sin_turns(0.5f * (float)i / (float)kWinSize);  // sin(pi x)
      float w = h * h;                                                // Hann
      w = clampf_(w * k, 0.f, 1.f);
      win_[i] = w;
    }
    ap_len_[0][0] = 887;
    ap_len_[0][1] = 1301;
    ap_len_[1][0] = 953;
    ap_len_[1][1] = 1187;
    reset();
  }

  // Clears everything at once. Fine at init, too slow for the audio thread.
  void reset() {
    resetState();
    clear_pos_ = 0;
    clearChunk(kBufSize);
  }

  // For resume: memory cleared over the next blocks, wet path silent.
  void resetSoft() {
    resetState();
    clear_pos_ = 0;
  }

  void resetState() {
    for (int g = 0; g < kMaxGrains; ++g) grains_[g].active = false;
    for (int ch = 0; ch < 2; ++ch) {
      for (int d = 0; d < kDiffusers; ++d)
        for (uint32_t i = 0; i < kApSize; ++i) mem_->ap[ch][d][i] = 0.f;
      fb_lp_[ch] = 0.f;
    }
    write_ = 0;
    ap_pos_ = 0;
    next_ = 0.f;
  }

  // TIME, [0, 1]: position, squared for finer control near "now".
  // Above 0.97: freeze.
  void setTime(float v) {
    v = clampf_(v, 0.f, 1.f);
    freeze_ = v > 0.97f;
    const float x = v / 0.97f;
    position_s_ = 0.005f + 4.9f * x * x;
  }

  // DEPTH, [0, 1]: density, bipolar around the centre.
  void setDensity(float v) {
    float x = 2.f * clampf_(v, 0.f, 1.f) - 1.f;
    if (fabsf_(x) < 0.03f) x = 0.f;
    density_ = x;
  }

  // SHIFT+DEPTH, [0, 1]: both at full level at the centre.
  void setMix(float v) { mix_ = clampf_(v, 0.f, 1.f); }

  // Number of grains playing right now (for CPU estimates on the host).
  int activeGrains() const {
    int c = 0;
    for (int g = 0; g < kMaxGrains; ++g) c += grains_[g].active ? 1 : 0;
    return c;
  }

  // io: stereo interleaved, processed in place.
  void process(float *io, uint32_t frames) {
    if (clear_pos_ < kBufSize) {
      clearChunk(kClearPerBlock);
      return;  // dry passes through untouched
    }
    const Voicing &vc = voicing_;
    const float inv = 1.f / (float)frames;

    // Overlap: how many grains play at once on average (1 to 12).
    const float amt = fabsf_(density_);
    const float overlap = 1.f + 11.f * amt * amt;
    const bool regular = density_ < 0.f;
    const float size = vc.size_s * kRate;
    // Mean time between grain starts, in samples.
    const float interval = size / overlap;
    const float norm = 1.f / sqrtf_(overlap);

    const float wet_t = mix_ < 0.5f ? 2.f * mix_ : 1.f;
    const float dry_t = mix_ > 0.5f ? 2.f * (1.f - mix_) : 1.f;
    const float wet_inc = (wet_t - wet_z_) * inv;
    const float dry_inc = (dry_t - dry_z_) * inv;
    float wz = wet_z_, dz = dry_z_;
    const float norm_inc = (norm - norm_z_) * inv;
    float nz = norm_z_;

    const float ap_g = 0.6f * vc.diffuse;
    const float fbk = freeze_ ? 0.f : vc.feedback;

    for (uint32_t n = 0; n < frames; ++n) {
      wz += wet_inc;
      dz += dry_inc;
      nz += norm_inc;

      // Grain scheduling.
      if (amt > 0.f) {
        next_ -= 1.f;
        if (next_ <= 0.f) {
          spawn(size);
          // Regular: fixed spacing. Random: exponential-ish spacing.
          const float r = rand01();
          next_ = regular ? interval : interval * 2.f * r * r * 1.5f;
          if (next_ < 16.f) next_ = 16.f;
        }
      }

      // Play grains.
      float wl = 0.f, wr = 0.f;
      for (int g = 0; g < kMaxGrains; ++g) {
        Grain &gr = grains_[g];
        if (!gr.active) continue;
        const float wp = gr.phase * (float)kWinSize;
        const int wi = (int)wp;
        const float w = win_[wi] + (wp - (float)wi) * (win_[wi + 1] - win_[wi]);

        const uint32_t i0 = (uint32_t)gr.pos;
        const float fr = gr.pos - (float)i0;
        const float *bl = mem_->buf[0];
        const float *br = mem_->buf[1];
        const float l = bl[i0 & kBufMask] + fr * (bl[(i0 + 1) & kBufMask] - bl[i0 & kBufMask]);
        const float rr = br[i0 & kBufMask] + fr * (br[(i0 + 1) & kBufMask] - br[i0 & kBufMask]);
        wl += w * (gr.gll * l + gr.glr * rr);
        wr += w * (gr.grl * l + gr.grr * rr);

        gr.pos += gr.rate;
        if (gr.pos >= (float)kBufSize) gr.pos -= (float)kBufSize;
        gr.phase += gr.inc;
        if (gr.phase >= 1.f) gr.active = false;
      }
      wl *= nz;
      wr *= nz;

      // Diffusion.
      float wet[2] = {wl, wr};
      if (ap_g > 0.f) {
        for (int ch = 0; ch < 2; ++ch) {
          float x = wet[ch];
          for (int d = 0; d < kDiffusers; ++d) {
            float *ap = mem_->ap[ch][d];
            const float delayed = ap[(ap_pos_ - ap_len_[ch][d]) & kApMask];
            const float v = x + ap_g * delayed;
            ap[ap_pos_ & kApMask] = v;
            x = delayed - ap_g * v;
          }
          wet[ch] = x;
        }
        ap_pos_ = (ap_pos_ + 1) & kApMask;
      }

      for (int ch = 0; ch < 2; ++ch) {
        const float x = io[2 * n + ch];
        if (!freeze_) {
          // Feedback is darkened and soft clipped so octave-up shimmer
          // cascades stay smooth and bounded.
          fb_lp_[ch] += 0.35f * (wet[ch] - fb_lp_[ch]);
          mem_->buf[ch][write_] = x + softclip(fbk * fb_lp_[ch]);
        }
        io[2 * n + ch] = softclip(dz * x + wz * wet[ch]);
      }
      if (!freeze_) write_ = (write_ + 1) & kBufMask;
    }

    wet_z_ = wet_t;
    dry_z_ = dry_t;
    norm_z_ = norm;
  }

 private:
  static const uint32_t kClearPerBlock = 16384;  // 32 blocks for a full clear

  struct Grain {
    bool active;
    float pos, rate, phase, inc;
    float gll, glr, grl, grr;  // stereo matrix: out L/R from in L/R
  };

  void spawn(float size_mean) {
    int slot = -1;
    for (int g = 0; g < kMaxGrains; ++g)
      if (!grains_[g].active) {
        slot = g;
        break;
      }
    if (slot < 0) return;  // all busy: skip this one
    const Voicing &vc = voicing_;
    Grain &gr = grains_[slot];

    const float len = size_mean * (1.f + vc.size_rand * (2.f * rand01() - 1.f) * 0.9f);
    const float semis = vc.pitches[(int)(rand01() * (float)vc.num_pitches) % vc.num_pitches] +
                        vc.detune_cents * 0.01f * (2.f * rand01() - 1.f);
    gr.rate = exp2f_(semis * (1.f / 12.f));

    // How far back to start. A grain reading faster than real time must
    // start far enough back not to overtake the write head.
    float back;
    if (freeze_) {
      back = (0.05f + 0.93f * rand01()) * (float)kBufSize;
    } else {
      back = position_s_ * kRate * (1.f + 0.15f * (2.f * rand01() - 1.f));
      const float min_back = len * (gr.rate > 1.f ? gr.rate : 1.f) + 64.f;
      if (back < min_back) back = min_back;
    }
    if (back > (float)(kBufSize - 1024)) back = (float)(kBufSize - 1024);
    gr.pos = (float)write_ - back;
    if (gr.pos < 0.f) gr.pos += (float)kBufSize;

    gr.phase = 0.f;
    gr.inc = 1.f / (len > 32.f ? len : 32.f);

    // Stereo: random balance and a little channel crossing with SPREAD.
    const float pan = 0.5f + 0.5f * vc.spread * (2.f * rand01() - 1.f);
    const float gl = cos_turns(0.25f * pan) * 1.41421356f;
    const float gr2 = sin_turns(0.25f * pan) * 1.41421356f;
    const float cross = 0.3f * vc.spread;
    gr.gll = gl * (1.f - cross);
    gr.glr = gl * cross;
    gr.grl = gr2 * cross;
    gr.grr = gr2 * (1.f - cross);
    gr.active = true;
  }

  void clearChunk(uint32_t n) {
    const uint32_t end = (clear_pos_ + n > kBufSize) ? kBufSize : clear_pos_ + n;
    for (int ch = 0; ch < 2; ++ch)
      for (uint32_t i = clear_pos_; i < end; ++i) mem_->buf[ch][i] = 0.f;
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

  Memory *mem_ = nullptr;
  Voicing voicing_ = {0.1f, 0.f, {0.f, 0.f, 0.f, 0.f}, 1, 0.f, 0.f, 0.f, 0.f, 0.f, 1};
  Grain grains_[kMaxGrains];
  float win_[kWinSize + 1];
  uint32_t ap_len_[2][kDiffusers];
  float fb_lp_[2] = {0.f, 0.f};
  uint32_t write_ = 0, ap_pos_ = 0, clear_pos_ = 0;
  uint32_t rng_ = 1;
  float next_ = 0.f;
  float position_s_ = 0.2f, density_ = 0.f, mix_ = 0.5f;
  bool freeze_ = false;
  float wet_z_ = 1.f, dry_z_ = 1.f, norm_z_ = 1.f;
};

}  // namespace grain
