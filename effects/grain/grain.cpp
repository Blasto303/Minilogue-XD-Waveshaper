// Granular reverb-slot FX for the KORG minilogue xd (logue SDK 1.1).
// Thin wrapper around common/grain_core.hpp.
//
// Controls: TIME = position (fully right = freeze), DEPTH = density
// (bipolar: left regular, right random, centre none), SHIFT+DEPTH = mix.
// Built 3 times from this file with a different fixed voicing:
//   GrnCloud  -DGRN_CLOUD    medium grains, no transposition
//   GrnShimr  -DGRN_SHIMMER  octave-up grains with feedback (shimmer)
//   GrnDust   -DGRN_DUST     tiny hard-edged grains, scattered pitches

#include "userrevfx.h"
#include "../../common/grain_core.hpp"

// size_s, size_rand, pitches[4], num_pitches, detune_cents, texture, spread,
// feedback, diffuse, seed
#if defined(GRN_SHIMMER)
static const grain::Voicing kVoicing = {0.18f, 0.3f, {0.f, 12.f, 12.f, 19.f}, 4, 3.f, 0.05f, 0.8f, 0.45f, 0.6f, 5};
#elif defined(GRN_DUST)
static const grain::Voicing kVoicing = {0.025f, 0.6f, {0.f, -12.f, 7.f, 12.f}, 4, 15.f, 0.8f, 1.f, 0.1f, 0.2f, 9};
#else
static const grain::Voicing kVoicing = {0.12f, 0.3f, {0.f, 0.f, 0.f, 0.f}, 1, 6.f, 0.1f, 0.6f, 0.f, 0.5f, 3};
#endif

static grain::Memory s_mem __sdram;
static grain::Grains s_grains;

void REVFX_INIT(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;
  s_grains.init(&s_mem, kVoicing);
}

void REVFX_PROCESS(float *xn, uint32_t frames) { s_grains.process(xn, frames); }

void REVFX_RESUME(void) { s_grains.resetSoft(); }

void REVFX_SUSPEND(void) {}

void REVFX_PARAM(uint8_t index, int32_t value) {
  const float v = (float)value * (1.f / 1023.f);
  switch (index) {
    case k_user_revfx_param_time: s_grains.setTime(v); break;
    case k_user_revfx_param_depth: s_grains.setDensity(v); break;
    case k_user_revfx_param_shift_depth: s_grains.setMix(v); break;
    default: break;
  }
}
