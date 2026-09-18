#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <string>
#include <vector>

#include "cholesky_kernels.h"

using Matrix9 = std::array<uint32_t, 9>;
using PatternSet = std::vector<Matrix9>;

constexpr size_t SIM_PATTERN_N = 65536;
constexpr uint32_t SIM_PATTERN_SEED = 0;
constexpr uint32_t SIM_L_DIAG_MIN = 256;
constexpr uint32_t SIM_L_DIAG_MAX = 1024;
constexpr uint32_t SIM_L_OFF_MIN = 0;
constexpr uint32_t SIM_L_OFF_MAX = 512;

struct KernelPatternData {
  uint32_t l00;
  uint32_t l10;
  uint32_t l20;
  uint32_t l11;
  uint32_t l21;
  uint32_t l22;
};

inline uint32_t mul_q8_8(uint32_t a, uint32_t b) {
  return static_cast<uint32_t>((static_cast<uint64_t>(a) * b) >> 8);
}

inline Matrix9 make_positive_definite_matrix(uint32_t l00, uint32_t l10, uint32_t l20,
                                             uint32_t l11, uint32_t l21, uint32_t l22) {
  Matrix9 A{};
  A[0] = mul_q8_8(l00, l00);
  A[1] = mul_q8_8(l10, l00);
  A[2] = mul_q8_8(l20, l00);
  A[3] = A[1];
  A[4] = mul_q8_8(l10, l10) + mul_q8_8(l11, l11);
  A[5] = mul_q8_8(l20, l10) + mul_q8_8(l21, l11);
  A[6] = A[2];
  A[7] = A[5];
  A[8] = mul_q8_8(l20, l20) + mul_q8_8(l21, l21) + mul_q8_8(l22, l22);
  return A;
}

inline KernelPatternData get_kernel_pattern_data(const Matrix9 &A) {
  KernelPatternData data{};
  kernel_cholesky_0(A[0], A[1], A[2], data.l00, data.l10, data.l20);
  kernel_cholesky_1(A[4], A[5], data.l10, data.l20, data.l11, data.l21);
  kernel_cholesky_2(A[8], data.l20, data.l21, data.l22);
  return data;
}

inline PatternSet generate_cholesky_input_patterns() {
  PatternSet input_values;
  input_values.reserve(SIM_PATTERN_N);

  std::mt19937 gen(SIM_PATTERN_SEED);
  std::uniform_int_distribution<uint32_t> diag_dist(SIM_L_DIAG_MIN, SIM_L_DIAG_MAX);
  std::uniform_int_distribution<uint32_t> off_dist(SIM_L_OFF_MIN, SIM_L_OFF_MAX);

  for (size_t i = 0; i < SIM_PATTERN_N; ++i) {
    uint32_t l00 = diag_dist(gen);
    uint32_t l10 = off_dist(gen);
    uint32_t l20 = off_dist(gen);
    uint32_t l11 = diag_dist(gen);
    uint32_t l21 = off_dist(gen);
    uint32_t l22 = diag_dist(gen);
    input_values.push_back(make_positive_definite_matrix(l00, l10, l20, l11, l21, l22));
  }

  return input_values;
}

inline void append_scalar_bits(std::string &line, uint32_t value, int width) {
  const uint32_t word = static_cast<uint32_t>(value);
  for (int bit = 0; bit < width; ++bit) {
    line.push_back(((word >> bit) & 1U) ? '1' : '0');
  }
}

inline void write_packed_bit_line(std::ofstream &packed_file, const std::string &name,
                                  const std::vector<uint64_t> &words) {
  packed_file << name;
  for (uint64_t word : words) {
    packed_file << ' ' << "0x" << std::hex << std::setw(16) << std::setfill('0')
                << word << std::dec;
  }
  packed_file << '\n';
}

struct KernelPortValue {
  std::string name;
  uint32_t value;
  int width;
};

inline std::vector<KernelPortValue> get_cholesky_kernel_inputs(size_t kernel, const Matrix9 &A,
                                                               const KernelPatternData &data) {
  const uint32_t l10_sq = mul_q8_8(data.l10, data.l10);
  const uint32_t prod = mul_q8_8(data.l20, data.l10);
  const int64_t diff2_signed = static_cast<int64_t>(A[5]) - prod;
  const uint32_t diff2 = diff2_signed <= 0 ? 0U : static_cast<uint32_t>(diff2_signed);
  switch (kernel) {
    case 0:
      return {{"a10", A[1], 16}, {"l00", data.l00, 16}};
    case 1:
      return {{"a20", A[2], 16}, {"l00", data.l00, 16}};
    case 2:
      (void)l10_sq;
      return {{"a11", A[4], 16}, {"a21", A[5], 16},
              {"l10", data.l10, 16}, {"l20", data.l20, 16}};
    case 3:
      return {{"diff2", diff2, 32}, {"l11", data.l11, 16}};
    case 4:
      return {{"a22", A[8], 16}, {"l20", data.l20, 16}, {"l21", data.l21, 16}};
    default:
      return {};
  }
}

inline void export_kernel_pattern_files(const PatternSet &input_values,
                                        const std::filesystem::path &kernel_dir) {
  std::filesystem::create_directories(kernel_dir);

  const std::array<const char *, 5> kernel_names = {
      "cholesky_k0_l10", "cholesky_k0_l20", "cholesky_k1_residual",
      "cholesky_k1_l21", "cholesky_k2_residual"};

  for (size_t kernel = 0; kernel < kernel_names.size(); ++kernel) {
    const std::filesystem::path als_path = kernel_dir / (std::string(kernel_names[kernel]) + ".als.pattern");
    const std::filesystem::path packed_path = kernel_dir / (std::string(kernel_names[kernel]) + ".als64.pattern");
    std::ofstream als_file(als_path);
    std::ofstream packed_file(packed_path);

    const size_t n_blocks = (input_values.size() + 63) / 64;
    const std::vector<KernelPortValue> first_values =
        get_cholesky_kernel_inputs(kernel, input_values[0], get_kernel_pattern_data(input_values[0]));
    std::vector<std::vector<uint64_t>> packed_words;
    std::vector<std::string> packed_names;
    for (const auto &port : first_values) {
      for (int bit = 0; bit < port.width; ++bit) {
        packed_names.push_back(port.name + "[" + std::to_string(bit) + "]");
        packed_words.emplace_back(n_blocks, 0);
      }
    }

    for (size_t frame = 0; frame < input_values.size(); ++frame) {
      const Matrix9 &A = input_values[frame];
      const KernelPatternData data = get_kernel_pattern_data(A);
      const std::vector<KernelPortValue> values = get_cholesky_kernel_inputs(kernel, A, data);

      std::string line;
      const size_t block_id = frame >> 6;
      const size_t bit_id = frame & 63U;

      size_t packed_index = 0;
      for (const auto &port : values) {
        append_scalar_bits(line, port.value, port.width);
        const uint32_t word = static_cast<uint32_t>(port.value);
        for (int bit = 0; bit < port.width; ++bit) {
          if ((word >> bit) & 1U) {
            packed_words[packed_index][block_id] |= (uint64_t{1} << bit_id);
          }
          ++packed_index;
        }
      }
      als_file << line << '\n';
    }

    for (size_t bit = 0; bit < packed_words.size(); ++bit) {
      write_packed_bit_line(packed_file, packed_names[bit], packed_words[bit]);
    }
  }
}
