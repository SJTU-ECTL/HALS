#pragma once
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
  const int64_t diff = std::llabs(static_cast<int64_t>(actual) - static_cast<int64_t>(expected));
  if (diff == 0) return 0.0;
  if (expected == 0) return 1.0;
  return static_cast<double>(diff) / static_cast<double>(std::llabs(static_cast<int64_t>(expected)));
}

#include "syr2k_kernels.h"

constexpr size_t SIM_PATTERN_N = 65536;
constexpr uint32_t SYR2K_ALPHA = 384;
constexpr uint32_t SYR2K_BETA = 307;

struct Syr2kPattern {
  uint32_t C_in;
  uint32_t A_jk;
  uint32_t B_ik;
  uint32_t B_jk;
  uint32_t A_ik;
};

inline std::vector<Syr2kPattern> generate_syr2k_input_patterns() {
  std::vector<Syr2kPattern> data;
  data.reserve(SIM_PATTERN_N);
  std::mt19937 gen(3003);
  std::uniform_int_distribution<uint32_t> dist(0, 512);
  for (size_t i = 0; i < SIM_PATTERN_N; ++i) {
    data.push_back({dist(gen), dist(gen), dist(gen), dist(gen), dist(gen)});
  }
  return data;
}

inline void export_kernel_pattern_files(const std::vector<Syr2kPattern> &data, const std::filesystem::path &dir) {
  std::filesystem::create_directories(dir);
  std::ofstream k1(dir / "syr2k_kernel1.als.pattern");
  std::ofstream k2(dir / "syr2k_kernel2.als.pattern");
  for (const auto &p : data) {
    uint32_t term2 = kernel_syr2k_1(p.A_jk, p.B_ik, p.B_jk, p.A_ik);
    std::string l1;
    append_scalar_bits(l1, p.A_jk);
    append_scalar_bits(l1, p.B_ik);
    append_scalar_bits(l1, p.B_jk);
    append_scalar_bits(l1, p.A_ik);
    k1 << l1 << '\n';
    std::string l2;
    append_scalar_bits(l2, SYR2K_BETA);
    append_scalar_bits(l2, p.C_in);
    append_scalar_bits(l2, SYR2K_ALPHA);
    append_scalar_bits(l2, term2);
    k2 << l2 << '\n';
  }
}
