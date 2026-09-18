#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <string>
#include <vector>

using Matrix3x3 = std::array<std::array<uint8_t, 3>, 3>;
using Matrix3x3Set = std::vector<Matrix3x3>;

constexpr size_t SIM_PATTERN_N = 65536;
constexpr uint32_t SIM_PATTERN_SEED = 0;
constexpr uint32_t SIM_INPUT_MIN = 0;
constexpr uint32_t SIM_INPUT_MAX = 255;

struct Conv3PatternSet {
  Matrix3x3Set a;
  Matrix3x3Set b;
};

inline uint32_t conv3(const Matrix3x3 &a, const Matrix3x3 &b) {
  uint32_t result = 0;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      result += static_cast<uint32_t>(a[i][j]) * static_cast<uint32_t>(b[i][j]);
    }
  }
  return result;
}

inline uint32_t conv3_add5(const Matrix3x3 &a, const Matrix3x3 &b) {
  const uint16_t conv0 = static_cast<uint16_t>(a[0][0]) * static_cast<uint16_t>(b[0][0]);
  const uint16_t conv1 = static_cast<uint16_t>(a[0][1]) * static_cast<uint16_t>(b[0][1]);
  const uint16_t conv2 = static_cast<uint16_t>(a[0][2]) * static_cast<uint16_t>(b[0][2]);
  const uint16_t conv3 = static_cast<uint16_t>(a[1][0]) * static_cast<uint16_t>(b[1][0]);
  const uint32_t add0 = conv0 + conv1;
  const uint32_t add1 = conv2 + conv3;
  return add0 + add1;
}

inline Conv3PatternSet generate_conv3_input_patterns() {
  Conv3PatternSet patterns;
  patterns.a.reserve(SIM_PATTERN_N);
  patterns.b.reserve(SIM_PATTERN_N);

  std::mt19937 gen(SIM_PATTERN_SEED);
  std::uniform_int_distribution<uint16_t> dist(SIM_INPUT_MIN, SIM_INPUT_MAX);

  for (size_t i = 0; i < SIM_PATTERN_N; ++i) {
    Matrix3x3 matrix_a{};
    Matrix3x3 matrix_b{};
    for (size_t j = 0; j < 3; ++j) {
      for (size_t k = 0; k < 3; ++k) {
        matrix_a[j][k] = static_cast<uint8_t>(dist(gen));
        matrix_b[j][k] = static_cast<uint8_t>(dist(gen));
      }
    }
    patterns.a.push_back(matrix_a);
    patterns.b.push_back(matrix_b);
  }

  return patterns;
}

inline void append_bits(std::string &line, uint32_t value, int width) {
  for (int bit = 0; bit < width; ++bit) {
    line.push_back(((value >> bit) & 1U) ? '1' : '0');
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

inline void export_conv3_kernel_patterns(const Conv3PatternSet &patterns,
                                         const std::filesystem::path &kernel_dir) {
  std::filesystem::create_directories(kernel_dir);
  const size_t n_blocks = (patterns.a.size() + 63) / 64;

  {
    std::ofstream als_file(kernel_dir / "conv3x3_kernel1.als.pattern");
    std::ofstream packed_file(kernel_dir / "conv3x3_kernel1.als64.pattern");
    std::vector<std::vector<uint64_t>> packed_words(8 * 8 * 2, std::vector<uint64_t>(n_blocks, 0));

    for (size_t frame = 0; frame < patterns.a.size(); ++frame) {
      std::string line;
      line.reserve(64);
      const size_t block_id = frame >> 6;
      const size_t bit_id = frame & 63U;

      for (int idx = 0; idx < 4; ++idx) {
        const int row = idx / 3;
        const int col = idx % 3;
        const uint8_t value = patterns.a[frame][row][col];
        append_bits(line, value, 8);
        for (int bit = 0; bit < 8; ++bit) {
          if ((value >> bit) & 1U) {
            packed_words[idx * 8 + bit][block_id] |= (uint64_t{1} << bit_id);
          }
        }
      }
      for (int idx = 0; idx < 4; ++idx) {
        const int row = idx / 3;
        const int col = idx % 3;
        const uint8_t value = patterns.b[frame][row][col];
        append_bits(line, value, 8);
        for (int bit = 0; bit < 8; ++bit) {
          if ((value >> bit) & 1U) {
            packed_words[32 + idx * 8 + bit][block_id] |= (uint64_t{1} << bit_id);
          }
        }
      }

      als_file << line << '\n';
    }

    for (int idx = 0; idx < 4; ++idx) {
      for (int bit = 0; bit < 8; ++bit) {
        write_packed_bit_line(packed_file,
                              "a" + std::to_string(idx) + "[" + std::to_string(bit) + "]",
                              packed_words[idx * 8 + bit]);
      }
    }
    for (int idx = 0; idx < 4; ++idx) {
      for (int bit = 0; bit < 8; ++bit) {
        write_packed_bit_line(
            packed_file,
            "b" + std::to_string(idx) + "[" + std::to_string(bit) + "]",
            packed_words[32 + idx * 8 + bit]);
      }
    }
  }

  {
    std::ofstream als_file(kernel_dir / "conv3x3_kernel2.als.pattern");
    std::ofstream packed_file(kernel_dir / "conv3x3_kernel2.als64.pattern");
    std::vector<std::vector<uint64_t>> packed_words((10 * 8) + 18, std::vector<uint64_t>(n_blocks, 0));

    for (size_t frame = 0; frame < patterns.a.size(); ++frame) {
      std::string line;
      line.reserve(98);
      const size_t block_id = frame >> 6;
      const size_t bit_id = frame & 63U;

      for (int idx = 4; idx <= 8; ++idx) {
        const int row = idx / 3;
        const int col = idx % 3;
        const uint8_t value = patterns.a[frame][row][col];
        append_bits(line, value, 8);
        for (int bit = 0; bit < 8; ++bit) {
          if ((value >> bit) & 1U) {
            packed_words[(idx - 4) * 8 + bit][block_id] |= (uint64_t{1} << bit_id);
          }
        }
      }
      for (int idx = 4; idx <= 8; ++idx) {
        const int row = idx / 3;
        const int col = idx % 3;
        const uint8_t value = patterns.b[frame][row][col];
        append_bits(line, value, 8);
        for (int bit = 0; bit < 8; ++bit) {
          if ((value >> bit) & 1U) {
            packed_words[40 + (idx - 4) * 8 + bit][block_id] |= (uint64_t{1} << bit_id);
          }
        }
      }

      const uint32_t add5 = conv3_add5(patterns.a[frame], patterns.b[frame]);
      append_bits(line, add5, 18);
      for (int bit = 0; bit < 18; ++bit) {
        if ((add5 >> bit) & 1U) {
          packed_words[80 + bit][block_id] |= (uint64_t{1} << bit_id);
        }
      }

      als_file << line << '\n';
    }

    for (int idx = 4; idx <= 8; ++idx) {
      for (int bit = 0; bit < 8; ++bit) {
        write_packed_bit_line(
            packed_file,
            "a" + std::to_string(idx) + "[" + std::to_string(bit) + "]",
            packed_words[(idx - 4) * 8 + bit]);
      }
    }
    for (int idx = 4; idx <= 8; ++idx) {
      for (int bit = 0; bit < 8; ++bit) {
        write_packed_bit_line(
            packed_file,
            "b" + std::to_string(idx) + "[" + std::to_string(bit) + "]",
            packed_words[40 + (idx - 4) * 8 + bit]);
      }
    }
    for (int bit = 0; bit < 18; ++bit) {
      write_packed_bit_line(packed_file, "add5[" + std::to_string(bit) + "]",
                            packed_words[80 + bit]);
    }
  }
}
