#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>


inline uint32_t q8_mul(uint32_t a, uint32_t b) {
  return static_cast<uint32_t>((static_cast<uint64_t>(a) * b) >> 8);
}

inline uint32_t q8_clip16(uint32_t x) {
  return static_cast<uint16_t>(x);
}

inline void append_scalar_bits(std::string &line, uint32_t value, int width = 16) {
  const uint32_t word = static_cast<uint32_t>(static_cast<uint16_t>(value));
  for (int bit = 0; bit < width; ++bit) {
    line.push_back(((word >> bit) & 1U) ? '1' : '0');
  }
}

inline double mape_term(uint32_t actual, uint32_t expected) {
  const int64_t diff = std::llabs(static_cast<int64_t>(actual) - expected);
  if (diff == 0) return 0.0;
  if (expected == 0) return 1.0;
  return static_cast<double>(diff) / static_cast<double>(std::llabs(static_cast<int64_t>(expected)));
}

#include "gesummv_kernels.h"

constexpr size_t SIM_PATTERN_N = 65536;
constexpr int GESUMMV_DEPTH = 15;
constexpr uint32_t GESUMMV_ALPHA = 384; // 1.5 in Q8.8
constexpr uint32_t GESUMMV_BETA = 307;  // about 1.2 in Q8.8

struct GesummvSample {
  uint32_t a;
  uint32_t b;
  uint32_t x;
};

using GesummvStream = std::vector<GesummvSample>;

inline GesummvStream generate_gesummv_input_patterns() {
  GesummvStream data;
  data.reserve(SIM_PATTERN_N);
  std::mt19937 gen(1001);
  std::uniform_int_distribution<uint32_t> dist(0, 512);
  for (size_t i = 0; i < SIM_PATTERN_N; ++i) {
    data.push_back({dist(gen), dist(gen), dist(gen)});
  }
  return data;
}

inline void export_kernel_pattern_files(const GesummvStream &data, const std::filesystem::path &dir) {
  std::filesystem::create_directories(dir);
  std::ofstream acc(dir / "gesummv_acc.als.pattern");
  std::ofstream comb(dir / "gesummv_combine.als.pattern");
  uint32_t tmp_acc = 0;
  uint32_t y_acc = 0;
  for (size_t i = 0; i < data.size(); ++i) {
    uint32_t tmp_next, y_next;
    kernel_gesummv_acc(tmp_acc, y_acc, data[i].a, data[i].b, data[i].x, tmp_next, y_next);
    std::string acc_line;
    append_scalar_bits(acc_line, tmp_acc);
    append_scalar_bits(acc_line, y_acc);
    append_scalar_bits(acc_line, data[i].a);
    append_scalar_bits(acc_line, data[i].b);
    append_scalar_bits(acc_line, data[i].x);
    acc << acc_line << '\n';
    std::string comb_line;
    append_scalar_bits(comb_line, GESUMMV_ALPHA);
    append_scalar_bits(comb_line, GESUMMV_BETA);
    append_scalar_bits(comb_line, tmp_next);
    append_scalar_bits(comb_line, y_next);
    comb << comb_line << '\n';
    tmp_acc = ((i + 1) % GESUMMV_DEPTH == 0) ? 0 : tmp_next;
    y_acc = ((i + 1) % GESUMMV_DEPTH == 0) ? 0 : y_next;
  }
}

inline uint32_t gesummv_step_exact(const GesummvStream &data, size_t end_index) {
  uint32_t tmp = 0;
  uint32_t y = 0;
  const size_t start = end_index + 1 - GESUMMV_DEPTH;
  for (size_t i = start; i <= end_index; ++i) {
    kernel_gesummv_acc(tmp, y, data[i].a, data[i].b, data[i].x, tmp, y);
  }
  return kernel_gesummv_combine(GESUMMV_ALPHA, GESUMMV_BETA, tmp, y);
}
