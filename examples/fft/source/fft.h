#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <string>
#include <utility>
#include <vector>

using Matrix16x2 = std::array<std::array<int32_t, 2>, 16>;
using PatternSet = std::vector<Matrix16x2>;

constexpr size_t SIM_PATTERN_N = 65536;
constexpr uint32_t SIM_PATTERN_SEED = 0;
constexpr int FFT_WORD_WIDTH = 16;
constexpr int FFT_FRAC_BITS = 8;
constexpr int32_t FFT_VALUE_MIN = -(1 << (FFT_WORD_WIDTH - 1));
constexpr int32_t FFT_VALUE_MAX = (1 << (FFT_WORD_WIDTH - 1)) - 1;
constexpr int32_t SIM_INPUT_MIN = -(1 << 14);
constexpr int32_t SIM_INPUT_MAX = (1 << 14) - 1;

enum class KernelInputSource {
  RawInput,
  Stage1Output,
  Stage2Output,
};

struct KernelPatternConfig {
  const char *module_name;
  KernelInputSource source;
  std::vector<int> sample_indices;
};

struct KernelPatternData {
  Matrix16x2 stage1;
  Matrix16x2 stage2;
};

inline int32_t sign_extend_q8_8(int32_t value) {
  return static_cast<int32_t>(static_cast<int16_t>(value));
}

inline int32_t trunc_q8_8(int64_t value) {
  return sign_extend_q8_8(static_cast<int32_t>(value >> FFT_FRAC_BITS));
}

inline void complex_mul_q8_8(int32_t real, int32_t imag, int32_t wr, int32_t wi,
                            int32_t &out_real, int32_t &out_imag) {
  int64_t tmp1 = static_cast<int64_t>(real) * static_cast<int64_t>(wr) -
                 static_cast<int64_t>(imag) * static_cast<int64_t>(wi);
  int64_t tmp2 = static_cast<int64_t>(real) * static_cast<int64_t>(wi) +
                 static_cast<int64_t>(imag) * static_cast<int64_t>(wr);
  out_real = trunc_q8_8(tmp1);
  out_imag = trunc_q8_8(tmp2);
}

inline void butterfly_plain(Matrix16x2 &sample, int a, int b) {
  const int32_t a_real = sample[a][0];
  const int32_t a_imag = sample[a][1];
  const int32_t b_real = sample[b][0];
  const int32_t b_imag = sample[b][1];
  sample[a][0] = sign_extend_q8_8(a_real + b_real);
  sample[a][1] = sign_extend_q8_8(a_imag + b_imag);
  sample[b][0] = sign_extend_q8_8(a_real - b_real);
  sample[b][1] = sign_extend_q8_8(a_imag - b_imag);
}

inline void butterfly_twiddle(Matrix16x2 &sample, int a, int b, int32_t wr, int32_t wi) {
  const int32_t a_real = sample[a][0];
  const int32_t a_imag = sample[a][1];
  const int32_t b_real = sample[b][0];
  const int32_t b_imag = sample[b][1];
  const int32_t diff_real = sign_extend_q8_8(a_real - b_real);
  const int32_t diff_imag = sign_extend_q8_8(a_imag - b_imag);
  int32_t rot_real = 0;
  int32_t rot_imag = 0;
  complex_mul_q8_8(diff_real, diff_imag, wr, wi, rot_real, rot_imag);
  sample[a][0] = sign_extend_q8_8(a_real + b_real);
  sample[a][1] = sign_extend_q8_8(a_imag + b_imag);
  sample[b][0] = rot_real;
  sample[b][1] = rot_imag;
}

inline Matrix16x2 fft_stage1(const Matrix16x2 &input) {
  Matrix16x2 sample = input;
  butterfly_plain(sample, 0, 8);
  butterfly_twiddle(sample, 1, 9, 0x00ed, static_cast<int32_t>(static_cast<int16_t>(0xff9e)));
  butterfly_twiddle(sample, 2, 10, 0x00b5, static_cast<int32_t>(static_cast<int16_t>(0xff4b)));
  butterfly_twiddle(sample, 3, 11, 0x0062, static_cast<int32_t>(static_cast<int16_t>(0xff13)));
  butterfly_twiddle(sample, 4, 12, 0x0000, static_cast<int32_t>(static_cast<int16_t>(0xff00)));
  butterfly_twiddle(sample, 5, 13, static_cast<int32_t>(static_cast<int16_t>(0xff9e)), static_cast<int32_t>(static_cast<int16_t>(0xff13)));
  butterfly_twiddle(sample, 6, 14, static_cast<int32_t>(static_cast<int16_t>(0xff4b)), static_cast<int32_t>(static_cast<int16_t>(0xff4b)));
  butterfly_twiddle(sample, 7, 15, static_cast<int32_t>(static_cast<int16_t>(0xff13)), static_cast<int32_t>(static_cast<int16_t>(0xff9e)));
  return sample;
}

inline Matrix16x2 fft_stage2(const Matrix16x2 &stage1) {
  Matrix16x2 sample = stage1;
  butterfly_plain(sample, 0, 4);
  butterfly_plain(sample, 8, 12);
  butterfly_twiddle(sample, 1, 5, 0x00b5, static_cast<int32_t>(static_cast<int16_t>(0xff4b)));
  butterfly_twiddle(sample, 9, 13, 0x00b5, static_cast<int32_t>(static_cast<int16_t>(0xff4b)));
  butterfly_twiddle(sample, 2, 6, 0x0000, static_cast<int32_t>(static_cast<int16_t>(0xff00)));
  butterfly_twiddle(sample, 10, 14, 0x0000, static_cast<int32_t>(static_cast<int16_t>(0xff00)));
  butterfly_twiddle(sample, 3, 7, static_cast<int32_t>(static_cast<int16_t>(0xff4b)), static_cast<int32_t>(static_cast<int16_t>(0xff4b)));
  butterfly_twiddle(sample, 11, 15, static_cast<int32_t>(static_cast<int16_t>(0xff4b)), static_cast<int32_t>(static_cast<int16_t>(0xff4b)));
  return sample;
}

inline KernelPatternData get_kernel_pattern_data(const Matrix16x2 &input) {
  KernelPatternData data{};
  data.stage1 = fft_stage1(input);
  data.stage2 = fft_stage2(data.stage1);
  return data;
}

inline const std::vector<KernelPatternConfig> &get_kernel_pattern_configs() {
  static const std::vector<KernelPatternConfig> configs = {
      {"fft_s1_2_1", KernelInputSource::RawInput, {0, 4, 8, 12}},
      {"fft_s1_2_2_1", KernelInputSource::RawInput, {1, 9}},
      {"fft_s1_2_2_2", KernelInputSource::RawInput, {5, 13}},
      {"fft_s1_2_2_3", KernelInputSource::Stage1Output, {1, 5}},
      {"fft_s1_2_2_4", KernelInputSource::Stage1Output, {9, 13}},
      {"fft_s1_2_3_1", KernelInputSource::RawInput, {2, 10}},
      {"fft_s1_2_3_2", KernelInputSource::RawInput, {6, 14}},
      {"fft_s1_2_3_3", KernelInputSource::Stage1Output, {2, 6}},
      {"fft_s1_2_3_4", KernelInputSource::Stage1Output, {10, 14}},
      {"fft_s1_2_4_1", KernelInputSource::RawInput, {3, 11}},
      {"fft_s1_2_4_2", KernelInputSource::RawInput, {7, 15}},
      {"fft_s1_2_4_3", KernelInputSource::Stage1Output, {3, 7}},
      {"fft_s1_2_4_4", KernelInputSource::Stage1Output, {11, 15}},
      {"fft_s3_4_1", KernelInputSource::Stage2Output, {0, 1, 2, 3}},
      {"fft_s3_4_2", KernelInputSource::Stage2Output, {4, 5, 6, 7}},
      {"fft_s3_4_3", KernelInputSource::Stage2Output, {8, 9, 10, 11}},
      {"fft_s3_4_4", KernelInputSource::Stage2Output, {12, 13, 14, 15}},
  };
  return configs;
}

inline Matrix16x2 select_kernel_source(const Matrix16x2 &input, const KernelPatternData &data,
                                       KernelInputSource source) {
  switch (source) {
  case KernelInputSource::RawInput:
    return input;
  case KernelInputSource::Stage1Output:
    return data.stage1;
  case KernelInputSource::Stage2Output:
    return data.stage2;
  }
  return input;
}

inline PatternSet generate_fft_input_patterns() {
  PatternSet input_values;
  input_values.reserve(SIM_PATTERN_N);
  std::mt19937 gen(SIM_PATTERN_SEED);
  std::uniform_int_distribution<int32_t> dist(SIM_INPUT_MIN, SIM_INPUT_MAX);
  for (size_t i = 0; i < SIM_PATTERN_N; ++i) {
    Matrix16x2 matrix{};
    for (size_t j = 0; j < 16; ++j) {
      matrix[j][0] = dist(gen);
      matrix[j][1] = dist(gen);
    }
    input_values.push_back(matrix);
  }
  return input_values;
}

inline void append_scalar_bits(std::string &line, int32_t value) {
  const uint32_t word = static_cast<uint16_t>(value);
  for (int bit = 0; bit < FFT_WORD_WIDTH; ++bit) {
    line.push_back(((word >> bit) & 1U) ? '1' : '0');
  }
}

inline void export_kernel_pattern_files(const PatternSet &input_values,
                                        const std::filesystem::path &kernel_dir) {
  std::filesystem::create_directories(kernel_dir);

  for (const auto &config : get_kernel_pattern_configs()) {
    const std::filesystem::path als_path =
        kernel_dir / (std::string(config.module_name) + ".als.pattern");
    const std::filesystem::path packed_path =
        kernel_dir / (std::string(config.module_name) + ".als64.pattern");
    std::ofstream als_file(als_path);
    std::ofstream packed_file(packed_path);

    std::vector<std::vector<uint64_t>> packed_words(config.sample_indices.size() * 2 * FFT_WORD_WIDTH);
    for (auto &port_words : packed_words) {
      port_words.assign((input_values.size() + 63) / 64, 0);
    }

    for (size_t frame = 0; frame < input_values.size(); ++frame) {
      const Matrix16x2 &input = input_values[frame];
      const KernelPatternData data = get_kernel_pattern_data(input);
      const Matrix16x2 source = select_kernel_source(input, data, config.source);

      std::string line;
      line.reserve(config.sample_indices.size() * 2 * FFT_WORD_WIDTH);
      size_t port_index = 0;
      for (int sample_index : config.sample_indices) {
        for (int part = 0; part < 2; ++part, ++port_index) {
          const int32_t value = source[sample_index][part];
          append_scalar_bits(line, value);
          const uint32_t word = static_cast<uint16_t>(value);
          const size_t block_id = frame >> 6;
          const size_t bit_id = frame & 63U;
          for (int bit = 0; bit < FFT_WORD_WIDTH; ++bit) {
            if ((word >> bit) & 1U) {
              packed_words[port_index * FFT_WORD_WIDTH + bit][block_id] |= (uint64_t{1} << bit_id);
            }
          }
        }
      }
      als_file << line << '\n';
    }

    for (size_t port_index = 0; port_index < config.sample_indices.size() * 2; ++port_index) {
      const int sample_index = config.sample_indices[port_index / 2];
      const int part = static_cast<int>(port_index % 2);
      for (int bit = 0; bit < FFT_WORD_WIDTH; ++bit) {
        packed_file << "sample_" << sample_index << "_" << part << "[" << bit << "]";
        for (uint64_t word : packed_words[port_index * FFT_WORD_WIDTH + bit]) {
          packed_file << ' ' << "0x" << std::hex << std::setw(16) << std::setfill('0')
                      << word << std::dec;
        }
        packed_file << '\n';
      }
    }
  }
}

inline void fft_raw(Matrix16x2 sample, Matrix16x2 &sample_out) {
  unsigned int index = 0;
  unsigned int N, M, len;

  M = 4;
  N = 16;
  len = N / 2;
  unsigned int stage, i, j, index2, windex, incr;
  int32_t tmp_real, tmp_imag, tmp_real2, tmp_imag2;
  const int32_t W[7][2] = {
      {0x00ed, static_cast<int32_t>(static_cast<int16_t>(0xff9e))},
      {0x00b5, static_cast<int32_t>(static_cast<int16_t>(0xff4b))},
      {0x0062, static_cast<int32_t>(static_cast<int16_t>(0xff13))},
      {0x0000, static_cast<int32_t>(static_cast<int16_t>(0xff00))},
      {static_cast<int32_t>(static_cast<int16_t>(0xff9e)), static_cast<int32_t>(static_cast<int16_t>(0xff13))},
      {static_cast<int32_t>(static_cast<int16_t>(0xff4b)), static_cast<int32_t>(static_cast<int16_t>(0xff4b))},
      {static_cast<int32_t>(static_cast<int16_t>(0xff13)), static_cast<int32_t>(static_cast<int16_t>(0xff9e))}};

  stage = 0;
  len = N;
  incr = 1;
  while (stage < M) {
    len = len / 2;

    i = 0;
    while (i < N) {
      index = i;
      index2 = index + len;

      tmp_real = sample[index][0] + sample[index2][0];
      tmp_imag = sample[index][1] + sample[index2][1];

      sample[index2][0] = sign_extend_q8_8(sample[index][0] - sample[index2][0]);
      sample[index2][1] = sign_extend_q8_8(sample[index][1] - sample[index2][1]);

      sample[index][0] = sign_extend_q8_8(tmp_real);
      sample[index][1] = sign_extend_q8_8(tmp_imag);

      i = i + 2 * len;
    }

    j = 1;
    windex = incr - 1;
#pragma clang loop unroll(enable)
    while (j < len) {
      i = j;
#pragma clang loop unroll(enable)
      while (i < N) {
        index = i;
        index2 = index + len;

        tmp_real = sign_extend_q8_8(sample[index][0] + sample[index2][0]);
        tmp_imag = sign_extend_q8_8(sample[index][1] + sample[index2][1]);
        tmp_real2 = sign_extend_q8_8(sample[index][0] - sample[index2][0]);
        tmp_imag2 = sign_extend_q8_8(sample[index][1] - sample[index2][1]);
        int64_t tmp1 = (static_cast<int64_t>(tmp_real2) *
                        static_cast<int64_t>(W[windex][0])) -
                       (static_cast<int64_t>(tmp_imag2) *
                        static_cast<int64_t>(W[windex][1]));
        int64_t tmp2 = (static_cast<int64_t>(tmp_real2) *
                        static_cast<int64_t>(W[windex][1])) +
                       (static_cast<int64_t>(tmp_imag2) *
                        static_cast<int64_t>(W[windex][0]));

        sample[index2][0] = trunc_q8_8(tmp1);
        sample[index2][1] = trunc_q8_8(tmp2);

        sample[index][0] = sign_extend_q8_8(tmp_real);
        sample[index][1] = sign_extend_q8_8(tmp_imag);

        i = i + 2 * len;
      }
      windex = windex + incr;
      j++;
    }

    stage++;
    incr = 2 * incr;
  }
  for (size_t t = 0; t < N; t++) {
    sample_out[t][0] = sample[t][0];
    sample_out[t][1] = sample[t][1];
  }
}
