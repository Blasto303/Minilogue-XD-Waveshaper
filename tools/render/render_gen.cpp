// Host renderer for the Swarm generator, alone and through Dungeon.
//
// Build and run: make -C tools/render
// Output: renders/3x_*.wav (48 kHz, 16-bit; mono alone, stereo through FX)

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../../common/dungeon_core.hpp"
#include "../../oscillators/swarm/swarm_core.hpp"
#include "wav.hpp"

static const int kBlock = 64;

static float note_w0(float note) {
  return 440.f * std::pow(2.f, (note - 69.f) / 12.f) / kRate;
}

// automate(p, t) sets params once per block and returns the MIDI note.
using Automation = std::function<float(swarm::Params &, float t)>;

static std::vector<float> render(float seconds, swarm::Params init, Automation automate) {
  static swarm::Swarm *sw = new swarm::Swarm;
  sw->init();
  sw->params = init;
  std::vector<float> out((size_t)(seconds * kRate));
  float buf[kBlock];
  for (size_t i = 0; i < out.size(); i += kBlock) {
    const float note = automate(sw->params, (float)i / kRate);
    const size_t n = std::min<size_t>(kBlock, out.size() - i);
    sw->process(buf, n, note_w0(note), 0.f);
    for (size_t j = 0; j < n; ++j) out[i + j] = 0.7f * buf[j];
  }
  return out;
}

static const float kPi = 3.14159265f;

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : "renders";

  // 1. MORPH sweep through the 8 cluster points, held low C.
  {
    swarm::Params p;
    p.span = 0.5f;
    p.seed = 7;
    p.scan = 3;
    write_wav(dir + "/30_swarm_morph_sweep.wav",
              render(12.f, p, [](swarm::Params &p, float t) {
                p.morph = t < 6.f ? t / 6.f : (12.f - t) / 6.f;
                return 36.f;
              }));
  }

  // 2. WARP from full left (phase modulation) to full right (exp. FM).
  {
    swarm::Params p;
    p.span = 0.4f;
    p.morph = 0.4f;
    p.seed = 3;
    p.scan = 11;
    write_wav(dir + "/31_swarm_warp_sweep.wav",
              render(12.f, p, [](swarm::Params &p, float t) {
                p.warp = t / 12.f;
                return 33.f;
              }));
  }

  // 3. Six SEEDs, 2 s each, MORPH drifting, wide SPAN.
  {
    swarm::Params p;
    p.span = 0.7f;
    p.scan = 5;
    write_wav(dir + "/32_swarm_seeds.wav",
              render(12.f, p, [](swarm::Params &p, float t) {
                p.seed = 10 + 17 * (int)(t / 2.f);
                p.morph = 0.5f + 0.4f * std::sin(2.f * kPi * 0.2f * t);
                return 40.f;
              }));
  }

  // 4. The 4 ALGOs, 3 s each, on a short phrase.
  {
    swarm::Params p;
    p.span = 0.8f;
    p.seed = 21;
    p.scan = 8;
    write_wav(dir + "/33_swarm_algos.wav",
              render(12.f, p, [](swarm::Params &p, float t) {
                p.algo = (int)(t / 3.f) % swarm::kNumAlgos;
                p.morph = std::fmod(t * 0.33f, 1.f);
                const float notes[] = {45, 48, 52, 43};
                return notes[(int)(t / 0.75f) % 4];
              }));
  }

  // 5. FIELD from full left (hollow comb) to full right (resonant comb).
  {
    swarm::Params p;
    p.span = 0.3f;
    p.morph = 0.6f;
    p.seed = 40;
    p.scan = 2;
    write_wav(dir + "/34_swarm_field_sweep.wav",
              render(12.f, p, [](swarm::Params &p, float t) {
                p.field = -1.f + 2.f * t / 12.f;
                return 38.f;
              }));
  }

  // 6. FUSE from full left (noise jitter) to full right (wavefolder).
  {
    swarm::Params p;
    p.span = 0.3f;
    p.morph = 0.3f;
    p.seed = 52;
    p.scan = 9;
    write_wav(dir + "/35_swarm_fuse_sweep.wav",
              render(12.f, p, [](swarm::Params &p, float t) {
                p.fuse = -1.f + 2.f * t / 12.f;
                return 36.f;
              }));
  }

  // 7. Whole chain, like the Vhikk X: Swarm -> low-pass + VCA (a rough
  //    stand-in for the xd's analog section) -> DngVoid with grains.
  {
    swarm::Params p;
    p.span = 0.6f;
    p.seed = 33;
    p.scan = 14;
    p.algo = swarm::kMinor;
    auto mono = render(20.f, p, [](swarm::Params &p, float t) {
      p.morph = 0.5f + 0.45f * std::sin(2.f * kPi * 0.07f * t);
      p.warp = 0.5f + 0.35f * std::sin(2.f * kPi * 0.031f * t + 1.f);
      return t < 10.f ? 31.f : 26.f;
    });
    // 2-pole low-pass, cutoff breathing slowly; fade in and out.
    std::vector<float> st(2 * mono.size());
    float s1 = 0.f, s2 = 0.f;
    for (size_t i = 0; i < mono.size(); ++i) {
      const float t = (float)i / kRate;
      const float fc = 300.f + 900.f * (0.5f + 0.5f * std::sin(2.f * kPi * 0.05f * t));
      const float g = 1.f - std::exp(-2.f * kPi * fc / kRate);
      s1 += g * (mono[i] - s1);
      s2 += g * (s1 - s2);
      const float vca = std::fmin(1.f, t / 1.5f) * std::fmin(1.f, (20.f - t) / 2.f);
      st[2 * i] = st[2 * i + 1] = 0.8f * s2 * vca;
    }
    // Same voicing as effects/dungeon/dungeon.cpp (DngVoid).
    const dungeon::Voicing kVoid = {1.f, -0.8f, 0.3f, 3, 3.5f, 45.f, 5.f, 15.f, 0.35f, 1, 1.35f, 0.f, 0.7f, 0.f};
    static dungeon::Memory *mem = new dungeon::Memory;
    dungeon::Dungeon d;
    d.init(mem, kVoid);
    for (size_t i = 0; i < mono.size(); i += kBlock) {
      const float t = (float)i / kRate;
      d.setTime(0.55f);
      d.setFeed(0.5f + 0.5f * std::fmin(0.85f, 0.3f + t / 20.f));
      d.setMix(0.6f);
      d.process(&st[2 * i], std::min<size_t>(kBlock, mono.size() - i));
    }
    write_wav(dir + "/36_swarm_through_dngvoid.wav", st, 2);
  }
  return 0;
}
