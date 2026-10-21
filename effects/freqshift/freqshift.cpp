// Frequency shifter MOD FX for the KORG minilogue xd (logue SDK 1.1).
// Thin wrapper around common/freqshift_core.hpp.
//
// Built twice from this file:
//   FreqShft  tight feedback loop (barber-pole phasing, metallic tones)
//   Spiral    -DFS_SPIRAL: 250 ms feedback delay, each echo shifted again

#include "usermodfx.h"
#include "../../common/freqshift_core.hpp"

#ifdef FS_SPIRAL
static const fshift::Config kConfig = {12000, 0.95f, 0.35f, true};
static float s_buf[2 * 12000] __sdram;
#else
static const fshift::Config kConfig = {1, 0.85f, 1.f, false};
static float s_buf[2];
#endif

static fshift::FreqShifter s_fs;

void MODFX_INIT(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;
  s_fs.init(s_buf, kConfig);
}

void MODFX_PROCESS(const float *main_xn, float *main_yn, const float *sub_xn,
                   float *sub_yn, uint32_t frames) {
  s_fs.process(main_xn, main_yn, frames);
  // The minilogue xd has no sub timbre: pass it through untouched.
  for (uint32_t i = 0; i < 2 * frames; ++i) sub_yn[i] = sub_xn[i];
}

void MODFX_RESUME(void) { s_fs.reset(); }

void MODFX_SUSPEND(void) {}

void MODFX_PARAM(uint8_t index, int32_t value) {
  const float v = (float)value * (1.f / 1023.f);
  switch (index) {
    case k_user_modfx_param_time: s_fs.setTime(v); break;
    case k_user_modfx_param_depth: s_fs.setDepth(v); break;
    default: break;
  }
}
