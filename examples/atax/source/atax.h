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

#include "atax_kernels.h"

constexpr size_t ATAX_CASES = 64;
constexpr int ATAX_M = 38;
constexpr int ATAX_N = 42;

struct AtaxMatrix {
  std::array<std::array<uint32_t, ATAX_N>, ATAX_M> a{};
  std::array<uint32_t, ATAX_N> x{};
};

using AtaxDataset = std::array<AtaxMatrix, ATAX_CASES>;
using AtaxOutputs = std::vector<uint32_t>;

inline AtaxDataset generate_atax_input_patterns() {
  AtaxDataset data{};
  std::mt19937 gen(20270828);
  std::uniform_int_distribution<uint32_t> dist_a(0, 320);
  std::uniform_int_distribution<uint32_t> dist_x(128, 512);
  for (size_t c = 0; c < ATAX_CASES; ++c) {
    for (int j = 0; j < ATAX_N; ++j) {
      data[c].x[j] = dist_x(gen);
    }
    for (int i = 0; i < ATAX_M; ++i) {
      for (int j = 0; j < ATAX_N; ++j) {
        data[c].a[i][j] = dist_a(gen);
      }
    }
  }
  return data;
}

inline AtaxOutputs atax_exact_outputs(const AtaxMatrix &matrix) {
  std::array<uint32_t, ATAX_N> y{};
  for (int i = 0; i < ATAX_M; ++i) {
    uint32_t tmp = 0;
    for (int j = 0; j < ATAX_N; ++j) {
      tmp = kernel_atax_tmp_acc(tmp, matrix.a[i][j], matrix.x[j]);
    }
    for (int j = 0; j < ATAX_N; ++j) {
      y[j] = kernel_atax_y_acc(y[j], matrix.a[i][j], tmp);
    }
  }
  AtaxOutputs outputs;
  outputs.reserve(ATAX_N);
  for (int j = 0; j < ATAX_N; ++j) {
    outputs.push_back(y[j]);
  }
  return outputs;
}

inline void export_atax_kernel_pattern_files(const AtaxDataset &data, const std::filesystem::path &dir) {
  std::filesystem::create_directories(dir);
  std::ofstream tmp_file(dir / "atax_tmp_acc.als.pattern");
  std::ofstream y_file(dir / "atax_y_acc.als.pattern");
  for (const auto &matrix : data) {
    std::array<uint32_t, ATAX_N> y{};
    for (int i = 0; i < ATAX_M; ++i) {
      uint32_t tmp = 0;
      for (int j = 0; j < ATAX_N; ++j) {
        std::string line;
        append_scalar_bits(line, tmp);
        append_scalar_bits(line, matrix.a[i][j]);
        append_scalar_bits(line, matrix.x[j]);
        tmp_file << line << '\n';
        tmp = kernel_atax_tmp_acc(tmp, matrix.a[i][j], matrix.x[j]);
      }
      for (int j = 0; j < ATAX_N; ++j) {
        std::string line;
        append_scalar_bits(line, y[j]);
        append_scalar_bits(line, matrix.a[i][j]);
        append_scalar_bits(line, tmp);
        y_file << line << '\n';
        y[j] = kernel_atax_y_acc(y[j], matrix.a[i][j], tmp);
      }
    }
  }
}
