#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <string>
#include <vector>

#include "decimation_model/decim_kernels.h"

using PatternSet = std::vector<int32_t>;

constexpr size_t SIM_PATTERN_N = 65536;
constexpr uint32_t SIM_PATTERN_SEED = 0;
constexpr int DECIM_WORD_WIDTH = 16;
constexpr int DECIM_FRAC_BITS = 8;
constexpr int32_t DECIM_VALUE_MIN = -(1 << (DECIM_WORD_WIDTH - 1));
constexpr int32_t DECIM_VALUE_MAX = (1 << (DECIM_WORD_WIDTH - 1)) - 1;
constexpr int32_t SIM_INPUT_MIN = -(1 << 14);
constexpr int32_t SIM_INPUT_MAX = (1 << 14) - 1;

struct KernelPatternData {
  int32_t stage1;
  int32_t stage2;
  int32_t stage3;
  int32_t stage4;
  int32_t stage5;
  // Full buffer contents at the moment this sample is processed
  std::vector<int32_t> buf1;   // 7 elements
  std::vector<int32_t> buf2;   // 7 elements
  std::vector<int32_t> buf3;   // 7 elements
  std::vector<int32_t> buf4;   // 11 elements
  std::vector<int32_t> buf5;   // 24 elements
};

// Run the full pipeline with history and collect per-sample buffer snapshots.
inline std::vector<KernelPatternData> run_pipeline_with_history(const PatternSet &input_values) {
  std::vector<KernelPatternData> result;
  result.reserve(input_values.size());
  DecimPipelineState st;
  for (size_t i = 0; i < input_values.size(); ++i) {
    KernelPatternData data{};
    // Run one clock step
    data.stage5 = decim_pipeline_step(input_values[i], st);
    // After step, st contains current history for each stage
    data.stage1 = st.buf1[0];
    data.stage2 = st.buf2[0];
    data.stage3 = st.buf3[0];
    data.stage4 = st.buf4[0];
    data.buf1.assign(st.buf1, st.buf1 + 7);
    data.buf2.assign(st.buf2, st.buf2 + 7);
    data.buf3.assign(st.buf3, st.buf3 + 7);
    data.buf4.assign(st.buf4, st.buf4 + 11);
    data.buf5.assign(st.buf5, st.buf5 + 24);
    result.push_back(data);
  }
  return result;
}

inline PatternSet generate_decimation_input_patterns() {
  PatternSet input_values;
  input_values.reserve(SIM_PATTERN_N);
  std::mt19937 gen(SIM_PATTERN_SEED);
  std::uniform_int_distribution<int32_t> dist(SIM_INPUT_MIN, SIM_INPUT_MAX);
  for (size_t i = 0; i < SIM_PATTERN_N; ++i) {
    input_values.push_back(dist(gen));
  }
  return input_values;
}

inline void append_scalar_bits(std::string &line, int32_t value, int width) {
  const uint32_t word = static_cast<uint16_t>(value);
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

// Get the full buffer inputs for a stage from the history snapshot
inline std::vector<int32_t> get_decim_stage_inputs(size_t stage,
                                                   const KernelPatternData &data) {
  switch (stage) {
    case 0: return data.buf1;
    case 1: return data.buf2;
    case 2: return data.buf3;
    case 3: return data.buf4;
    case 4: return data.buf5;
    default: return {};
  }
}

inline std::vector<int32_t> get_decim_kernel_inputs(size_t kernel,
                                                    const KernelPatternData &data) {
  if (kernel < 4) {
    return get_decim_stage_inputs(kernel, data);
  }
  const auto &buf5 = data.buf5;
  switch (kernel) {
    case 4:
      return {buf5[0], buf5[1], buf5[2], buf5[3],
              buf5[20], buf5[21], buf5[22], buf5[23]};
    case 5:
      return {buf5[4], buf5[5], buf5[6], buf5[7],
              buf5[16], buf5[17], buf5[18], buf5[19]};
    case 6:
      return {buf5[8], buf5[9], buf5[10], buf5[11],
              buf5[13], buf5[14], buf5[15]};
    default:
      return {};
  }
}

inline void export_kernel_pattern_files(const PatternSet &input_values,
                                        const std::filesystem::path &kernel_dir) {
  std::filesystem::create_directories(kernel_dir);

  // Run the full pipeline with history to get real buffer contents
  const auto history_data = run_pipeline_with_history(input_values);

  const std::array<const char *, 7> kernel_names = {
      "decim_stage1", "decim_stage2", "decim_stage3", "decim_stage4",
      "decim_stage5_p0", "decim_stage5_p1", "decim_stage5_p2"};
  const std::array<int, 7> port_counts = {7, 7, 7, 11, 8, 8, 7};
  constexpr int port_width = DECIM_WORD_WIDTH;
  const std::array<std::vector<int>, 7> port_indices = {{
      {0, 1, 2, 3, 4, 5, 6},
      {0, 1, 2, 3, 4, 5, 6},
      {0, 1, 2, 3, 4, 5, 6},
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
      {0, 1, 2, 3, 20, 21, 22, 23},
      {4, 5, 6, 7, 16, 17, 18, 19},
      {8, 9, 10, 11, 13, 14, 15},
  }};

  for (size_t stage = 0; stage < kernel_names.size(); ++stage) {
    const std::filesystem::path als_path = kernel_dir / (std::string(kernel_names[stage]) + ".als.pattern");
    const std::filesystem::path packed_path = kernel_dir / (std::string(kernel_names[stage]) + ".als64.pattern");
    std::ofstream als_file(als_path);
    std::ofstream packed_file(packed_path);

    const size_t n_blocks = (history_data.size() + 63) / 64;
    std::vector<std::vector<uint64_t>> packed_words(port_counts[stage] * port_width,
                                                   std::vector<uint64_t>(n_blocks, 0));

    for (size_t frame = 0; frame < history_data.size(); ++frame) {
      const std::vector<int32_t> values = get_decim_kernel_inputs(stage, history_data[frame]);

      std::string line;
      line.reserve(port_counts[stage] * port_width);
      const size_t block_id = frame >> 6;
      const size_t bit_id = frame & 63U;

      for (int port = 0; port < port_counts[stage]; ++port) {
        append_scalar_bits(line, values[port], port_width);
        const uint32_t word = static_cast<uint16_t>(values[port]);
        for (int bit = 0; bit < port_width; ++bit) {
          if ((word >> bit) & 1U) {
            packed_words[port * port_width + bit][block_id] |= (uint64_t{1} << bit_id);
          }
        }
      }
      als_file << line << '\n';
    }

    for (int port = 0; port < port_counts[stage]; ++port) {
      for (int bit = 0; bit < port_width; ++bit) {
        write_packed_bit_line(packed_file,
                              "buf_" + std::to_string(port_indices[stage][port]) + "[" + std::to_string(bit) + "]",
                              packed_words[port * port_width + bit]);
      }
    }
  }
}
