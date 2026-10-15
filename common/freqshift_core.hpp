// Frequency shifter (Bode style) with feedback. DSP core.
//
// Plain C++11, no logue SDK dependency: the same code runs on the
// minilogue xd and in the host renderer (tools/render).
//
// Per channel:
//   s = x + feedback * (delayed y, high-passed, damped, soft clipped)
//   (I, Q) = Hilbert(s)               90 degree phase pair
//   y = I*cos(phi) + Q*sin(phi)       single sideband: every partial moves
//                                     by the same number of Hz
//   y is written into the feedback delay line.
// The right channel's carrier runs 90 degrees ahead of the left, so slow
// shifts rotate across the stereo field.

#pragma once

#include <stdint.h>

#include "dsp_math.hpp"

namespace fshift {

using namespace dspm;

// Hilbert transformer: two chains of four 2nd-order allpass sections
// (coefficients by Olli Niemitalo). Under 1 degree of phase error from about
// 20 Hz to 20 kHz at 48 kHz.
class Hilbert {
 public:
  void reset() {
    for (int i = 0; i < 4; ++i) {
      a_[i].reset();
      b_[i].reset();
    }
    i_z_ = 0.f;
  }

  DSPM_INLINE_MEMBER void process(float x, float &i_out, float &q_out) {
    static const float ka[4] = {0.6923878f * 0.6923878f,
                                0.9360654322959f * 0.9360654322959f,
                                0.9882295226860f * 0.9882295226860f,
                                0.9987488452737f * 0.9987488452737f};
    static const float kb[4] = {0.4021921162426f * 0.4021921162426f,
                                0.8561710882420f * 0.8561710882420f,
                                0.9722909545651f * 0.9722909545651f,
                                0.9952884791278f * 0.9952884791278f};
    float a = x, b = x;
    for (int k = 0; k < 4; ++k) {
      a = a_[k].process(a, ka[k]);
      b = b_[k].process(b, kb[k]);
    }
    // Path A is delayed by one sample to line up with path B.
    i_out = i_z_;
    i_z_ = a;
    q_out = b;
  }

 private:
  struct Section {
    float x1, x2, y1, y2;
    void reset() { x1 = x2 = y1 = y2 = 0.f; }
    DSPM_INLINE_MEMBER float process(float x, float c) {
      const float y = c * (x + y2) - x2;
      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = y;
      return y;
    }
  };
  Section a_[4], b_[4];
  float i_z_ = 0.f;
};

struct Config {
  uint32_t delay_frames;  // feedback delay length, 1 = tight loop
  float max_feedback;     // feedback at DEPTH 100 %
  float damp;             // one-pole low-pass in the loop, 1 = open
  bool keep_dry;          // true: dry stays at full level (echo style)
};

class FreqShifter {
 public:
  static constexpr float kMaxShiftHz = 2000.f;
  static constexpr float kRate = 48000.f;

  // buf: stereo interleaved, 2 * config.delay_frames floats.
  void init(float *buf, const Config &config) {
    buf_ = buf;
    cfg_ = config;
    reset();
  }

  void reset() {
    for (uint32_t i = 0; i < 2 * cfg_.delay_frames; ++i) buf_[i] = 0.f;
    pos_ = 0;
    for (int c = 0; c < 2; ++c) {
      hil_[c].reset();
      hp_x1_[c] = hp_y1_[c] = lp_[c] = 0.f;
    }
    phase_ = 0.f;
  }

  // TIME knob, [0, 1]. Centre = no shift, left = down, right = up.
  // |shift| = 2000 Hz * |x|^4: fine control of slow shifts near the centre
  // (about 0.2 Hz at 10 % off centre, 16 Hz at 30 %, 125 Hz at 50 %).
  void setTime(float v) {
    float x = 2.f * clampf_(v, 0.f, 1.f) - 1.f;
    const float ax = fabsf_(x);
    float hz = (ax < 0.01f) ? 0.f : kMaxShiftHz * ax * ax * ax * ax;
    shift_hz_ = (x < 0.f) ? -hz : hz;
  }

  // DEPTH knob, [0, 1]. First half: wet level 0 -> 100 %.
  // Second half: feedback 0 -> max.
  void setDepth(float v) { depth_ = clampf_(v, 0.f, 1.f); }

  float shiftHz() const { return shift_hz_; }

  // in/out: stereo interleaved, frames * 2 floats. In-place is fine.
  void process(const float *in, float *out, uint32_t frames) {
    const float inv = 1.f / (float)frames;

    const float w = shift_hz_ / kRate;
    const float w_inc = (w - w_z_) * inv;
    const float mix = depth_ < 0.5f ? 2.f * depth_ : 1.f;
    const float fb = depth_ > 0.5f ? (2.f * depth_ - 1.f) * cfg_.max_feedback : 0.f;
    const float mix_inc = (mix - mix_z_) * inv;
    const float fb_inc = (fb - fb_z_) * inv;

    float wz = w_z_, mz = mix_z_, fz = fb_z_;
    float phase = phase_;
    const uint32_t len = cfg_.delay_frames;
    const float damp = cfg_.damp;

    for (uint32_t n = 0; n < frames; ++n) {
      wz += w_inc;
      mz += mix_inc;
      fz += fb_inc;

      const float c = cos_turns(phase);
      const float s = sin_turns(phase);
      // Carrier per channel. Right is 90 degrees ahead:
      // cos(p + 1/4) = -sin(p), sin(p + 1/4) = cos(p).
      const float cc[2] = {c, -s};
      const float ss[2] = {s, c};

      float *slot = buf_ + 2 * pos_;
      for (int ch = 0; ch < 2; ++ch) {
        const float x = in[2 * n + ch];

        // Feedback: high-pass (~20 Hz) so down-shifts don't pile up at DC,
        // optional damping, soft clip so the loop always stays bounded.
        const float d = slot[ch];
        const float hp = d - hp_x1_[ch] + 0.9974f * hp_y1_[ch];
        hp_x1_[ch] = d;
        hp_y1_[ch] = hp;
        lp_[ch] += damp * (hp - lp_[ch]);
        const float sig = x + softclip(fz * lp_[ch]);

        float i, q;
        hil_[ch].process(sig, i, q);
        const float y = i * cc[ch] + q * ss[ch];

        slot[ch] = y;  // read happened above: this is a len-frame delay
        // Echo style keeps the dry signal and adds the wet at -3 dB.
        out[2 * n + ch] = cfg_.keep_dry ? softclip(x + 0.7f * mz * y)
                                        : softclip(x + mz * (y - x));
      }

      if (++pos_ >= len) pos_ = 0;
      phase += wz;
      phase -= floorf_(phase);
    }

    phase_ = phase;
    w_z_ = w;
    mix_z_ = mix;
    fb_z_ = fb;
  }

 private:
  float *buf_ = nullptr;
  Config cfg_ = {1, 0.9f, 1.f, false};
  uint32_t pos_ = 0;
  Hilbert hil_[2];
  float hp_x1_[2] = {0.f, 0.f}, hp_y1_[2] = {0.f, 0.f}, lp_[2] = {0.f, 0.f};
  float phase_ = 0.f;
  float shift_hz_ = 0.f, depth_ = 0.f;
  float w_z_ = 0.f, mix_z_ = 0.f, fb_z_ = 0.f;
};

}  // namespace fshift
