// Minimal 16-bit PCM WAV writer shared by the renderers.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static const int kRate = 48000;

// x: interleaved samples, `channels` per frame.
static void write_wav(const std::string &path, const std::vector<float> &x,
                      int channels = 1) {
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
  u16(channels);
  u32(kRate);
  u32(kRate * 2 * channels);
  u16(2 * channels);
  u16(16);
  std::fwrite("data", 1, 4, f);
  u32(data_bytes);
  float peak = 0.f;
  for (float s : x) {
    peak = std::fmax(peak, std::fabs(s));
    const float c = s > 1.f ? 1.f : (s < -1.f ? -1.f : s);
    const int16_t v = (int16_t)std::lrint(c * 32767.f);
    std::fwrite(&v, 2, 1, f);
  }
  std::fclose(f);
  std::printf("wrote %s (%.1f s, peak %.2f)\n", path.c_str(),
              (double)x.size() / channels / kRate, peak);
}
