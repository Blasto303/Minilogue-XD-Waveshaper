// Host renderer: runs the oscillator core on a computer and writes WAV files,
// so sounds can be checked before loading anything into the minilogue xd.
//
// Build and run: make -C tools/render
// Output: renders/*.wav (48 kHz, 16-bit, mono)

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../../oscillators/waveshaper/waveshaper_core.hpp"

static const int kRate = 48000;
static const int kBlock = 64;  // same block size as the hardware

static float note_w0(float note) {
  return 440.f * std::pow(2.f, (note - 69.f) / 12.f) / kRate;
}

static void write_wav(const std::string &path, const std::vector<float> &x) {
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) {
    std::perror(path.c_str());
    return;
  }
  const uint32_t data_bytes = x.size() * 2;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f);
  u32(36 + data_bytes);
  std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);  // PCM
  u16(1);  // mono
  u32(kRate);
  u32(kRate * 2);
  u16(2);
  u16(16);
  std::fwrite("data", 1, 4, f);
  u32(data_bytes);
  for (float s : x) {
    const float c = s > 1.f ? 1.f : (s < -1.f ? -1.f : s);
    const int16_t v = (int16_t)std::lrint(c * 32767.f);
    std::fwrite(&v, 2, 1, f);
  }
  std::fclose(f);
  std::printf("wrote %s (%.1f s)\n", path.c_str(), (double)x.size() / kRate);
}

// Simple amp envelope so note sequences don't click.
static float env(float t, float len) {
  const float a = 0.005f, r = 0.05f;
  if (t < a) return t / a;
  if (t > len - r) return std::fmax(0.f, (len - t) / r);
  return 1.f;
}

// Called once per block with the time in seconds; sets params and returns
// the MIDI note (fractional allowed) to play.
using Automation = std::function<float(wsh::Params &, float t)>;

static std::vector<float> render(float seconds, const wsh::Params &init,
                                 Automation automate, float gain = 0.7f) {
  wsh::Oscillator osc;
  osc.params = init;
  osc.reset();
  std::vector<float> out((size_t)(seconds * kRate));
  float buf[kBlock];
  int last_type = osc.params.type;
  for (size_t i = 0; i < out.size(); i += kBlock) {
    const float t = (float)i / kRate;
    const float note = automate(osc.params, t);
    if (osc.params.type != last_type) {
      osc.onTypeChange();
      last_type = osc.params.type;
    }
    const size_t n = std::min<size_t>(kBlock, out.size() - i);
    osc.process(buf, n, note_w0(note), 0.f);
    for (size_t j = 0; j < n; ++j) out[i + j] = buf[j] * gain;
  }
  return out;
}

// Plays a short phrase, with a per-note hook to change params.
static std::vector<float> phrase(const std::vector<float> &notes, float note_len,
                                 const wsh::Params &init,
                                 std::function<void(wsh::Params &, int, float)> hook) {
  std::vector<float> out;
  wsh::Oscillator osc;
  osc.params = init;
  float buf[kBlock];
  for (size_t k = 0; k < notes.size(); ++k) {
    osc.reset();  // the hardware resets on note on
    const size_t len = (size_t)(note_len * kRate);
    int last_type = osc.params.type;
    for (size_t i = 0; i < len; i += kBlock) {
      const float t = (float)i / kRate;
      hook(osc.params, (int)k, t / note_len);
      if (osc.params.type != last_type) {
        osc.onTypeChange();
        last_type = osc.params.type;
      }
      const size_t n = std::min<size_t>(kBlock, len - i);
      osc.process(buf, n, note_w0(notes[k]), 0.f);
      for (size_t j = 0; j < n; ++j)
        out.push_back(0.7f * buf[j] * env((float)(i + j) / kRate, note_len));
    }
  }
  return out;
}

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : "renders";
  const char *names[] = {"sinefold", "trifold", "saturate"};

  // 1. SHAPE sweep 0 -> 100 % -> 0 for each shaper, note C2 (36).
  for (int type = 0; type < wsh::kNumShapers; ++type) {
    wsh::Params p;
    p.type = type;
    write_wav(dir + "/01_shape_sweep_" + names[type] + ".wav",
              render(6.f, p, [](wsh::Params &p, float t) {
                p.shape = t < 3.f ? t / 3.f : (6.f - t) / 3.f;
                return 36.f;
              }));
  }

  // 2. Bias sweep (SHIFT+SHAPE) at half fold, sine fold.
  {
    wsh::Params p;
    p.shape = 0.5f;
    write_wav(dir + "/02_bias_sweep_sinefold.wav",
              render(6.f, p, [](wsh::Params &p, float t) {
                p.shiftshape = t < 3.f ? t / 3.f : (6.f - t) / 3.f;
                return 36.f;
              }));
  }

  // 3. Source morph + sub with a slow fold LFO.
  {
    wsh::Params p;
    p.sub = 0.6f;
    write_wav(dir + "/03_source_sub_lfo.wav",
              render(8.f, p, [](wsh::Params &p, float t) {
                p.source = t / 8.f;
                p.shape = 0.35f + 0.3f * std::sin(2.f * 3.14159265f * 0.5f * t);
                return 33.f;
              }));
  }

  // 4. A/B aliasing test: high note, full fold, without then with ADAA.
  {
    wsh::Params p;
    p.shape = 1.f;
    p.adaa = false;
    auto a = render(2.f, p, [](wsh::Params &, float t) { return 84.f + 12.f * t; });
    p.adaa = true;
    auto b = render(2.f, p, [](wsh::Params &, float t) { return 84.f + 12.f * t; });
    a.insert(a.end(), b.begin(), b.end());
    write_wav(dir + "/04_aliasing_ab_off_then_on.wav", a);
  }

  // 5. Bass phrase, fold follows each note from high to low ("pluck").
  {
    wsh::Params p;
    p.damp = 0.3f;
    p.sub = 0.3f;
    p.shiftshape = 0.2f;
    const std::vector<float> notes = {36, 36, 48, 36, 39, 36, 46, 43,
                                      36, 36, 48, 36, 41, 39, 34, 31};
    write_wav(dir + "/05_bass_phrase_pluck.wav",
              phrase(notes, 0.2f, p, [](wsh::Params &p, int k, float x) {
                p.type = (k / 8) % 2 ? wsh::kTriFold : wsh::kSineFold;
                p.shape = 0.9f * std::exp(-4.f * x);
              }));
  }

  // 6. Lead phrase with key tracking and saturation.
  {
    wsh::Params p;
    p.type = wsh::kSaturate;
    p.keytrack = 0.7f;
    p.shape = 0.8f;
    p.source = 0.5f;
    const std::vector<float> notes = {60, 63, 67, 70, 72, 75, 79, 82,
                                      84, 82, 79, 75, 72, 70, 67, 63};
    write_wav(dir + "/06_lead_keytrack_saturate.wav",
              phrase(notes, 0.18f, p, [](wsh::Params &, int, float) {}));
  }
  return 0;
}
