// Dungeon delay FX for the KORG minilogue xd (logue SDK 1.1).
// Thin wrapper around common/dungeon_core.hpp.
//
// Controls: TIME = echo time (10 ms to 1.5 s), DEPTH = FEED (bipolar,
// centre = none), SHIFT+DEPTH = dry/wet mix.
// Built 3 times from this file with a different fixed voicing:
//   DngEcho   -DDNG_ECHO   distinct echoes, slow shift
//   DngMetal  -DDNG_METAL  dense metallic taps, inharmonic shift
//   DngVoid   -DDNG_VOID   diffuse cloud, slow downward drift

#include "userdelfx.h"
#include "../../common/dungeon_core.hpp"

//                                        form  cell_hz  damp  seed
#if defined(DNG_METAL)
static const dungeon::Voicing kVoicing = {0.6f, 23.f, 0.8f, 7};
#elif defined(DNG_VOID)
static const dungeon::Voicing kVoicing = {1.f, -0.35f, 0.35f, 3};
#else
static const dungeon::Voicing kVoicing = {0.12f, 1.5f, 0.6f, 1};
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
