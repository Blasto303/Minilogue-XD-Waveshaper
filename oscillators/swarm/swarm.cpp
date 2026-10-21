// Swarm: user oscillator for the KORG minilogue xd (logue SDK 1.1).
// Thin wrapper around swarm_core.hpp.
//
// SHAPE = MORPH (cluster scan), SHIFT+SHAPE = WARP (centre = neutral).
// Menu: Span, Field, Fuse, Scan, Seed, Algo.

#include "userosc.h"
#include "swarm_core.hpp"

static swarm::Swarm s_swarm;
static float s_buf[64];

void OSC_INIT(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;
  s_swarm.init();
}

void OSC_CYCLE(const user_osc_param_t *const params, int32_t *yn,
               const uint32_t frames) {
  const float w0 = osc_w0f_for_note(params->pitch >> 8, params->pitch & 0xFF);
  const float lfo = q31_to_f32(params->shape_lfo);
  uint32_t done = 0;
  while (done < frames) {
    const uint32_t n = (frames - done > 64) ? 64 : frames - done;
    s_swarm.process(s_buf, n, w0, lfo);
    for (uint32_t i = 0; i < n; ++i) yn[done + i] = f32_to_q31(s_buf[i]);
    done += n;
  }
}

// Free-running oscillators: no phase reset on note on, like a drone voice.
void OSC_NOTEON(const user_osc_param_t *const params) { (void)params; }

void OSC_NOTEOFF(const user_osc_param_t *const params) { (void)params; }

void OSC_PARAM(uint16_t index, uint16_t value) {
  swarm::Params &p = s_swarm.params;
  switch (index) {
    case k_user_osc_param_id1: p.span = clip01f(value * 0.01f); break;
    // Bipolar percent: 0..200, 100 = 0 %.
    case k_user_osc_param_id2: p.field = clip1m1f(((int)value - 100) * 0.01f); break;
    case k_user_osc_param_id3: p.fuse = clip1m1f(((int)value - 100) * 0.01f); break;
    case k_user_osc_param_id4: p.scan = value > 100 ? 100 : value; break;
    case k_user_osc_param_id5: p.seed = value > 100 ? 100 : value; break;
    case k_user_osc_param_id6: p.algo = value % swarm::kNumAlgos; break;
    case k_user_osc_param_shape: p.morph = param_val_to_f32(value); break;
    case k_user_osc_param_shiftshape: p.warp = param_val_to_f32(value); break;
    default: break;
  }
}
