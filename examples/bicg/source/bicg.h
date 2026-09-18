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

#include "bicg_kernels.h"

constexpr size_t BICG_CASES = 64;
constexpr int BICG_M = 38;
constexpr int BICG_N = 42;

struct BicgMatrix {
  std::array<std::array<uint32_t, BICG_M>, BICG_N> a{};
  std::array<uint32_t, BICG_M> p{};
  std::array<uint32_t, BICG_N> r{};
};

using BicgDataset = std::array<BicgMatrix, BICG_CASES>;
using BicgOutputs = std::vector<uint32_t>;

inline BicgDataset generate_bicg_input_patterns() {
  BicgDataset data{};
  std::mt19937 gen(20270827);
  std::uniform_int_distribution<uint32_t> dist_a(0, 384);
  std::uniform_int_distribution<uint32_t> dist_v(128, 512);
  for (size_t c = 0; c < BICG_CASES; ++c) {
    for (int j = 0; j < BICG_M; ++j) {
      data[c].p[j] = dist_v(gen);
    }
    for (int i = 0; i < BICG_N; ++i) {
      data[c].r[i] = dist_v(gen);
      for (int j = 0; j < BICG_M; ++j) {
        data[c].a[i][j] = dist_a(gen);
      }
    }
  }
  return data;
}

inline BicgOutputs bicg_exact_outputs(const BicgMatrix &matrix) {
  std::array<uint32_t, BICG_M> s{};
  BicgOutputs outputs;
  outputs.reserve(BICG_N + BICG_M);
  for (int i = 0; i < BICG_N; ++i) {
    uint32_t q = 0;
    for (int j = 0; j < BICG_M; ++j) {
      s[j] = kernel_bicg_s_acc(s[j], matrix.a[i][j], matrix.r[i]);
      q = kernel_bicg_q_acc(q, matrix.a[i][j], matrix.p[j]);
    }
    outputs.push_back(q);
  }
  for (int j = 0; j < BICG_M; ++j) {
    outputs.push_back(s[j]);
  }
  return outputs;
}

inline void export_bicg_kernel_pattern_files(const BicgDataset &data, const std::filesystem::path &dir) {
  std::filesystem::create_directories(dir);
  std::ofstream s_file(dir / "bicg_s_acc.als.pattern");
  std::ofstream q_file(dir / "bicg_q_acc.als.pattern");
  for (const auto &matrix : data) {
    std::array<uint32_t, BICG_M> s{};
    for (int i = 0; i < BICG_N; ++i) {
      uint32_t q = 0;
      for (int j = 0; j < BICG_M; ++j) {
        std::string s_line;
        append_scalar_bits(s_line, s[j]);
        append_scalar_bits(s_line, matrix.a[i][j]);
        append_scalar_bits(s_line, matrix.r[i]);
        s_file << s_line << '\n';

        std::string q_line;
        append_scalar_bits(q_line, q);
        append_scalar_bits(q_line, matrix.a[i][j]);
        append_scalar_bits(q_line, matrix.p[j]);
        q_file << q_line << '\n';

        s[j] = kernel_bicg_s_acc(s[j], matrix.a[i][j], matrix.r[i]);
        q = kernel_bicg_q_acc(q, matrix.a[i][j], matrix.p[j]);
      }
    }
  }
}
