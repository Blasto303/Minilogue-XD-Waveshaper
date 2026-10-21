// Dungeon delay FX for the KORG minilogue xd (logue SDK 1.1).
// Thin wrapper around common/dungeon_core.hpp.
//
// Controls: TIME = echo time (10 ms to 1.5 s), DEPTH = FEED (bipolar,
// centre = none, self-oscillates past ~70 % either way), SHIFT+DEPTH =
// dry/wet mix.
// Built 4 times from this file with a different fixed voicing:
//   DngGrowl  -DDNG_GROWL  dark loop, downward shift into sub rumble, fast chaos
//   DngMetal  -DDNG_METAL  dense taps, upward shift, light drive
//   DngBell   -DDNG_BELL   upward shift into a ringing 320 Hz resonance
//   DngVoid   -DDNG_VOID   diffuse cloud, slow sink, deep resonance

#include "userdelfx.h"
#include "../../common/dungeon_core.hpp"

// form, cell_hz, damp, seed, drive, growl_hz, growl_q, fm, chaos, crush, max_feed
#if defined(DNG_METAL)
static const dungeon::Voicing kVoicing = {0.75f, 31.f, 0.6f, 7, 2.5f, 160.f, 7.f, 20.f, 0.6f, 1, 1.2f};
#elif defined(DNG_BELL)
static const dungeon::Voicing kVoicing = {0.85f, 31.f, 0.45f, 7, 2.f, 320.f, 12.f, 30.f, 1.f, 1, 1.2f};
#elif defined(DNG_VOID)
static const dungeon::Voicing kVoicing = {1.f, -0.8f, 0.3f, 3, 3.5f, 45.f, 5.f, 15.f, 0.35f, 1, 1.35f};
#else
static const dungeon::Voicing kVoicing = {0.8f, -7.f, 0.25f, 1, 4.f, 60.f, 8.f, 15.f, 0.8f, 1, 1.25f};
#endif

static dungeon::Memory s_mem __sdram;
static dungeon::Dungeon s_dng;

void DELFX_INIT(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;
  s_dng.init(&s_mem, kVoicing);
}

void DELFX_PROCESS(float *xn, uint32_t frames) { s_dng.process(xn, frames); }

void DELFX_RESUME(void) { s_dng.resetSoft(); }

void DELFX_SUSPEND(void) {}

void DELFX_PARAM(uint8_t index, int32_t value) {
  const float v = (float)value * (1.f / 1023.f);
  switch (index) {
    case k_user_delfx_param_time: s_dng.setTime(v); break;
    case k_user_delfx_param_depth: s_dng.setFeed(v); break;
    case k_user_delfx_param_shift_depth: s_dng.setMix(v); break;
    default: break;
  }
}
