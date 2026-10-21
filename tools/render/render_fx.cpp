// Host renderer for the MOD FX units: synthesizes test material (pad, drums,
// arpeggio), runs it through the effect core and writes stereo WAV files.
//
// Build and run: make -C tools/render
// Output: renders/1x_*.wav (48 kHz, 16-bit, stereo)

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../../common/dungeon_core.hpp"
#include "../../common/freqshift_core.hpp"
#include "wav.hpp"

static const int kBlock = 64;

// --- Test material ---------------------------------------------------------

static float hz(float note) { return 440.f * std::pow(2.f, (note - 69.f) / 12.f); }

// Band-limited saw (polyBLEP).
struct Saw {
  float ph = 0.f;
  float tick(float f) {
    const float dt = f / kRate;
    float y = 2.f * ph - 1.f;
    if (ph < dt) {
      const float t = ph / dt;
      y -= t + t - t * t - 1.f;
    } else if (ph > 1.f - dt) {
      const float t = (ph - 1.f) / dt;
      y -= t * t + t + t + 1.f;
    }
    ph += dt;
    if (ph >= 1.f) ph -= 1.f;
    return y;
  }
};

static uint32_t s_seed = 1;
static float noise() {
  s_seed = s_seed * 1664525u + 1013904223u;
  return (float)(int32_t)s_seed * (1.f / 2147483648.f);
}

// Minor-7 pad, 2 detuned saws per note, slow low-pass. Stereo interleaved.
static std::vector<float> pad(float seconds) {
  const float notes[] = {48, 55, 58, 63};
  Saw saws[8];
  float lp = 0.f;
  const size_t n = (size_t)(seconds * kRate);
  std::vector<float> out(2 * n);
  for (size_t i = 0; i < n; ++i) {
    const float t = (float)i / kRate;
    float x = 0.f;
    for (int k = 0; k < 4; ++k)
      x += saws[2 * k].tick(hz(notes[k]) * 1.003f) + saws[2 * k + 1].tick(hz(notes[k]) * 0.997f);
    const float cut = 0.04f + 0.03f * std::sin(2.f * 3.14159f * 0.15f * t);
    lp += cut * (x - lp);
    const float a = std::fmin(1.f, t / 0.3f) * std::fmin(1.f, (seconds - t) / 0.3f);
    out[2 * i] = out[2 * i + 1] = 0.12f * lp * a;
  }
  return out;
}

// Kick / snare / hat loop at 120 BPM.
static std::vector<float> drums(float seconds) {
  const size_t n = (size_t)(seconds * kRate);
  const size_t step = kRate / 8;  // 16th notes
  std::vector<float> out(2 * n);
  const char *kick = "x...x...x..x.x..";
  const char *snare = "....x.......x...";
  float hp_x1 = 0.f, hp_y1 = 0.f;
  for (size_t i = 0; i < n; ++i) {
    const size_t s = (i / step) % 16;
    const float t = (float)(i % step) / kRate;
    float x = 0.f;
    if (kick[s] == 'x') {
      const float ph = 50.f * t + 1.8f * (1.f - std::exp(-t * 30.f));
      x += 0.9f * std::sin(2.f * 3.14159f * ph) * std::exp(-t * 9.f);
    }
    const float nz = noise();
    const float hp = nz - hp_x1 + 0.6f * hp_y1;  // crude high-pass for hats
    hp_x1 = nz;
    hp_y1 = hp;
    if (snare[s] == 'x')
      x += 0.5f * (0.6f * nz + 0.4f * std::sin(2.f * 3.14159f * 190.f * t)) *
           std::exp(-t * 18.f);
    if (s % 2 == 0) x += 0.25f * hp * std::exp(-t * 70.f);
    out[2 * i] = out[2 * i + 1] = 0.6f * x;
  }
  return out;
}

// Plucky saw arpeggio followed by silence (to hear the feedback tail).
static std::vector<float> arp(float seconds, float play_seconds) {
  const float notes[] = {60, 63, 67, 70, 72, 70, 67, 63};
  const size_t n = (size_t)(seconds * kRate);
  const size_t step = kRate / 6;
  std::vector<float> out(2 * n);
  Saw saw;
  float lp = 0.f;
  for (size_t i = 0; i < n; ++i) {
    const float t = (float)(i % step) / kRate;
    float x = 0.f;
    if (i < (size_t)(play_seconds * kRate)) {
      const float e = std::exp(-t * 12.f);
      x = saw.tick(hz(notes[(i / step) % 8]));
      lp += (0.02f + 0.3f * e) * (x - lp);
      x = 0.35f * lp * e;
    }
    out[2 * i] = out[2 * i + 1] = x;
  }
  return out;
}

// --- Effect runner ---------------------------------------------------------

// TIME knob value for a given shift in Hz (inverse of setTime's mapping).
static float time_for_hz(float shift) {
  const float ax = std::pow(std::fabs(shift) / fshift::FreqShifter::kMaxShiftHz, 0.25f);
  return 0.5f + (shift < 0.f ? -0.5f : 0.5f) * ax;
}

// automate(t) sets TIME and DEPTH once per block, like knob moves.
using Automation = std::function<void(fshift::FreqShifter &, float t)>;

static std::vector<float> run(const fshift::Config &cfg, std::vector<float> x,
                              Automation automate) {
  std::vector<float> buf(2 * cfg.delay_frames);
  fshift::FreqShifter fs;
  fs.init(buf.data(), cfg);
  const size_t frames = x.size() / 2;
  for (size_t i = 0; i < frames; i += kBlock) {
    automate(fs, (float)i / kRate);
    const size_t n = std::min<size_t>(kBlock, frames - i);
    fs.process(&x[2 * i], &x[2 * i], n);
  }
  return x;
}

// --- Dungeon --------------------------------------------------------------

// Same voicings as effects/dungeon/dungeon.cpp.
// form, cell_hz, damp, seed, drive, growl_hz, growl_q, fm, chaos, crush, max_feed
static const dungeon::Voicing kGrowl = {0.8f, -7.f, 0.25f, 1, 4.f, 60.f, 8.f, 15.f, 0.8f, 1, 1.25f};
static const dungeon::Voicing kMetal = {0.75f, 31.f, 0.6f, 7, 2.5f, 160.f, 7.f, 20.f, 0.6f, 1, 1.2f};
static const dungeon::Voicing kBell = {0.85f, 31.f, 0.45f, 7, 2.f, 320.f, 12.f, 30.f, 1.f, 1, 1.2f};
static const dungeon::Voicing kVoid = {1.f, -0.8f, 0.3f, 3, 3.5f, 45.f, 5.f, 15.f, 0.35f, 1, 1.35f};

// TIME knob value for an echo time in seconds (inverse of setTime).
static float time_for_s(float s) {
  return std::log(s / dungeon::Dungeon::kMinTime) /
         std::log(dungeon::Dungeon::kMaxTime / dungeon::Dungeon::kMinTime);
}

// FEED in [-1, 1] -> DEPTH knob value.
static float depth_for_feed(float f) { return 0.5f + 0.5f * f; }

using DngAutomation = std::function<void(dungeon::Dungeon &, float t)>;

static std::vector<float> run_dng(const dungeon::Voicing &v, std::vector<float> x,
                                  DngAutomation automate) {
  static dungeon::Memory *mem = new dungeon::Memory;
  dungeon::Dungeon d;
  d.init(mem, v);
  const size_t frames = x.size() / 2;
  for (size_t i = 0; i < frames; i += kBlock) {
    automate(d, (float)i / kRate);
    const size_t n = std::min<size_t>(kBlock, frames - i);
    d.process(&x[2 * i], n);
  }
  return x;
}

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : "renders";
  // Same settings as effects/freqshift/freqshift.cpp.
  const fshift::Config tight = {1, 0.85f, 1.f, false};
  const fshift::Config spiral = {12000, 0.95f, 0.35f, true};

  // 1. Dry references.
  write_wav(dir + "/10_dry_pad.wav", pad(8.f), 2);
  write_wav(dir + "/10_dry_drums.wav", drums(8.f), 2);

  // 2. FreqShft, 100 % wet, TIME sweeps 0 -> +300 Hz -> 0 -> -300 Hz -> 0.
  write_wav(dir + "/11_freqshft_sweep_pad.wav",
            run(tight, pad(10.f), [](fshift::FreqShifter &fs, float t) {
              const float s = std::sin(2.f * 3.14159f * t / 10.f);
              fs.setTime(time_for_hz(300.f * s * std::fabs(s)));
              fs.setDepth(0.5f);
            }), 2);

  // 3. FreqShft barber pole: tiny shift, feedback. Up 4 s, then down 4 s.
  write_wav(dir + "/12_freqshft_barberpole_pad.wav",
            run(tight, pad(8.f), [](fshift::FreqShifter &fs, float t) {
              fs.setTime(time_for_hz(t < 4.f ? 0.4f : -0.4f));
              fs.setDepth(0.85f);
            }), 2);

  // 4. FreqShft on drums: +45 Hz, 60 % wet. Metallic, out of tune.
  write_wav(dir + "/13_freqshft_metal_drums.wav",
            run(tight, drums(8.f), [](fshift::FreqShifter &fs, float t) {
              fs.setTime(time_for_hz(t < 4.f ? 45.f : -45.f));
              fs.setDepth(0.3f);
            }), 2);

  // 5. Spiral, each echo 40 Hz higher.
  write_wav(dir + "/14_spiral_up_arp.wav",
            run(spiral, arp(9.f, 4.f), [](fshift::FreqShifter &fs, float) {
              fs.setTime(time_for_hz(40.f));
              fs.setDepth(0.85f);
            }), 2);

  // 6. Spiral, each echo 15 Hz lower, near-max feedback.
  write_wav(dir + "/15_spiral_down_arp.wav",
            run(spiral, arp(10.f, 4.f), [](fshift::FreqShifter &fs, float) {
              fs.setTime(time_for_hz(-15.f));
              fs.setDepth(1.f);
            }), 2);

  // 7. Stress test: DEPTH 100 %, TIME swept across the full range.
  write_wav(dir + "/16_freqshft_max_feedback_sweep.wav",
            run(tight, pad(8.f), [](fshift::FreqShifter &fs, float t) {
              fs.setTime(t / 8.f);
              fs.setDepth(1.f);
            }), 2);

  // --- Dungeon ---

  // 1. Self-oscillation from silence: no input at all. FEED climbs from
  //    centre to 95 % over 4 s, then TIME wanders. One variant after another.
  {
    std::vector<float> all;
    const dungeon::Voicing vs[] = {kGrowl, kMetal, kBell, kVoid};
    const float side[] = {-1.f, 1.f, -1.f, -1.f};
    for (int i = 0; i < 4; ++i) {
      const float sd = side[i];
      auto y = run_dng(vs[i], std::vector<float>(2 * 9 * kRate, 0.f),
                       [sd](dungeon::Dungeon &d, float t) {
                         d.setFeed(depth_for_feed(sd * std::fmin(0.95f, 0.5f + 0.45f * t / 4.f)));
                         d.setTime(time_for_s(0.06f + 0.05f * std::sin(0.9f * t) * (t > 4.f)));
                         d.setMix(1.f);
                       });
      all.insert(all.end(), y.begin(), y.end());
    }
    write_wav(dir + "/20_dungeon_from_silence.wav", all, 2);
  }

  // 2. DngGrowl on the pad: FEED left, pushed into self-oscillation.
  write_wav(dir + "/21_dnggrowl_pad.wav",
            run_dng(kGrowl, pad(12.f), [](dungeon::Dungeon &d, float t) {
              d.setTime(time_for_s(0.18f));
              d.setFeed(depth_for_feed(-0.5f - 0.45f * std::fmin(1.f, t / 8.f)));
              d.setMix(0.6f);
            }), 2);

  // 3. DngMetal on drums: FEED swept from full left to full right.
  write_wav(dir + "/22_dngmetal_drums_feed_sweep.wav",
            run_dng(kMetal, drums(12.f), [](dungeon::Dungeon &d, float t) {
              d.setTime(time_for_s(0.045f));
              d.setFeed(depth_for_feed(-1.f + 2.f * t / 12.f));
              d.setMix(0.6f);
            }), 2);

  // 4. DngMetal on the arpeggio, TIME turned while it screams.
  write_wav(dir + "/23_dngmetal_arp_time_bend.wav",
            run_dng(kMetal, arp(12.f, 4.f), [](dungeon::Dungeon &d, float t) {
              const float s = t < 5.f ? 0.12f : (t < 9.f ? 0.12f + 0.1f * (t - 5.f) : 0.52f);
              d.setTime(time_for_s(s));
              d.setFeed(depth_for_feed(-0.85f));
              d.setMix(0.7f);
            }), 2);

  // 5. DngBell on the pad, same gestures as demo 21.
  write_wav(dir + "/25_dngbell_pad.wav",
            run_dng(kBell, pad(12.f), [](dungeon::Dungeon &d, float t) {
              d.setTime(time_for_s(0.18f));
              d.setFeed(depth_for_feed(-0.5f - 0.45f * std::fmin(1.f, t / 8.f)));
              d.setMix(0.6f);
            }), 2);

  // 6. DngVoid on the pad, notch side.
  write_wav(dir + "/24_dngvoid_pad.wav",
            run_dng(kVoid, pad(12.f), [](dungeon::Dungeon &d, float) {
              d.setTime(time_for_s(0.45f));
              d.setFeed(depth_for_feed(0.8f));
              d.setMix(0.7f);
            }), 2);

  // 7. Stress test: every voicing, FEED at both extremes, short and long time.
  {
    std::vector<float> all;
    const dungeon::Voicing vs[] = {kGrowl, kMetal, kBell, kVoid};
    for (const auto &v : vs)
      for (float feed : {-1.f, 1.f})
        for (float tm : {0.01f, 1.5f}) {
          auto y = run_dng(v, drums(3.f), [feed, tm](dungeon::Dungeon &d, float) {
            d.setTime(time_for_s(tm));
            d.setFeed(depth_for_feed(feed));
            d.setMix(1.f);
          });
          all.insert(all.end(), y.begin(), y.end());
        }
    write_wav(dir + "/26_dungeon_stress_test.wav", all, 2);
  }
  return 0;
}
