// Waveshaper: user oscillator for the KORG minilogue xd (logue SDK 1.1).
// Thin wrapper around waveshaper_core.hpp.

#include "userosc.h"
#include "waveshaper_core.hpp"

static wsh::Oscillator s_osc;
static float s_buf[64];

void OSC_INIT(uint32_t platform, uint32_t api) {
  (void)platform;
  (void)api;
  s_osc.reset();
}

void OSC_CYCLE(const user_osc_param_t *const params, int32_t *yn,
               const uint32_t frames) {
  const float w0 = osc_w0f_for_note(params->pitch >> 8, params->pitch & 0xFF);
  const float lfo = q31_to_f32(params->shape_lfo);

  // The API guarantees at most 64 frames, but stay safe.
  uint32_t done = 0;
  while (done < frames) {
    const uint32_t n = (frames - done > 64) ? 64 : frames - done;
    s_osc.process(s_buf, n, w0, lfo);
    for (uint32_t i = 0; i < n; ++i) yn[done + i] = f32_to_q31(s_buf[i]);
    done += n;
  }
}

void OSC_NOTEON(const user_osc_param_t *const params) {
  (void)params;
  s_osc.reset();
}

void OSC_NOTEOFF(const user_osc_param_t *const params) { (void)params; }

void OSC_PARAM(uint16_t index, uint16_t value) {
  wsh::Params &p = s_osc.params;
  switch (index) {
    case k_user_osc_param_id1: {  // Type
      const int type = value % wsh::kNumShapers;
      if (type != p.type) {
        p.type = type;
        s_osc.onTypeChange();
      }
    } break;
    case k_user_osc_param_id2: p.source = clip01f(value * 0.01f); break;
    case k_user_osc_param_id3: p.sub = clip01f(value * 0.01f); break;
    case k_user_osc_param_id4: p.keytrack = clip01f(value * 0.01f); break;
    case k_user_osc_param_id5: p.damp = clip01f(value * 0.01f); break;
    case k_user_osc_param_id6: p.dry = clip01f(value * 0.01f); break;
    case k_user_osc_param_shape: p.shape = param_val_to_f32(value); break;
    case k_user_osc_param_shiftshape:
      p.shiftshape = param_val_to_f32(value);
      break;
    default: break;
  }
}
