#include "Vfft_wrapper.h"
#include "verilated.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

#include "fft.h"

void test_fft(const PatternSet &input_values, std::vector<Matrix16x2> &approx_results,
              std::vector<Matrix16x2> &exact_results) {
  Vfft_wrapper *fft_module = new Vfft_wrapper;

  for (size_t i = 0; i < input_values.size(); ++i) {
    Matrix16x2 approx_result{};
    Matrix16x2 exact_result{};
      fft_module->sample_0_0 = input_values[i][0][0];
      fft_module->sample_0_1 = input_values[i][0][1];
      fft_module->sample_1_0 = input_values[i][1][0];
      fft_module->sample_1_1 = input_values[i][1][1];
      fft_module->sample_2_0 = input_values[i][2][0];
      fft_module->sample_2_1 = input_values[i][2][1];
      fft_module->sample_3_0 = input_values[i][3][0];
      fft_module->sample_3_1 = input_values[i][3][1];
      fft_module->sample_4_0 = input_values[i][4][0];
      fft_module->sample_4_1 = input_values[i][4][1];
      fft_module->sample_5_0 = input_values[i][5][0];
      fft_module->sample_5_1 = input_values[i][5][1];
      fft_module->sample_6_0 = input_values[i][6][0];
      fft_module->sample_6_1 = input_values[i][6][1];
      fft_module->sample_7_0 = input_values[i][7][0];
      fft_module->sample_7_1 = input_values[i][7][1];
      fft_module->sample_8_0 = input_values[i][8][0];
      fft_module->sample_8_1 = input_values[i][8][1];
      fft_module->sample_9_0 = input_values[i][9][0];
      fft_module->sample_9_1 = input_values[i][9][1];
      fft_module->sample_10_0 = input_values[i][10][0];
      fft_module->sample_10_1 = input_values[i][10][1];
      fft_module->sample_11_0 = input_values[i][11][0];
      fft_module->sample_11_1 = input_values[i][11][1];
      fft_module->sample_12_0 = input_values[i][12][0];
      fft_module->sample_12_1 = input_values[i][12][1];
      fft_module->sample_13_0 = input_values[i][13][0];
      fft_module->sample_13_1 = input_values[i][13][1];
      fft_module->sample_14_0 = input_values[i][14][0];
      fft_module->sample_14_1 = input_values[i][14][1];
      fft_module->sample_15_0 = input_values[i][15][0];
      fft_module->sample_15_1 = input_values[i][15][1];

    fft_module->eval();
    approx_result[0][0] = sign_extend_q8_8(fft_module->sample_out_0_0);
    approx_result[0][1] = sign_extend_q8_8(fft_module->sample_out_0_1);
    approx_result[1][0] = sign_extend_q8_8(fft_module->sample_out_1_0);
    approx_result[1][1] = sign_extend_q8_8(fft_module->sample_out_1_1);
    approx_result[2][0] = sign_extend_q8_8(fft_module->sample_out_2_0);
    approx_result[2][1] = sign_extend_q8_8(fft_module->sample_out_2_1);
    approx_result[3][0] = sign_extend_q8_8(fft_module->sample_out_3_0);
    approx_result[3][1] = sign_extend_q8_8(fft_module->sample_out_3_1);
    approx_result[4][0] = sign_extend_q8_8(fft_module->sample_out_4_0);
    approx_result[4][1] = sign_extend_q8_8(fft_module->sample_out_4_1);
    approx_result[5][0] = sign_extend_q8_8(fft_module->sample_out_5_0);
    approx_result[5][1] = sign_extend_q8_8(fft_module->sample_out_5_1);
    approx_result[6][0] = sign_extend_q8_8(fft_module->sample_out_6_0);
    approx_result[6][1] = sign_extend_q8_8(fft_module->sample_out_6_1);
    approx_result[7][0] = sign_extend_q8_8(fft_module->sample_out_7_0);
    approx_result[7][1] = sign_extend_q8_8(fft_module->sample_out_7_1);
    approx_result[8][0] = sign_extend_q8_8(fft_module->sample_out_8_0);
    approx_result[8][1] = sign_extend_q8_8(fft_module->sample_out_8_1);
    approx_result[9][0] = sign_extend_q8_8(fft_module->sample_out_9_0);
    approx_result[9][1] = sign_extend_q8_8(fft_module->sample_out_9_1);
    approx_result[10][0] = sign_extend_q8_8(fft_module->sample_out_10_0);
    approx_result[10][1] = sign_extend_q8_8(fft_module->sample_out_10_1);
    approx_result[11][0] = sign_extend_q8_8(fft_module->sample_out_11_0);
    approx_result[11][1] = sign_extend_q8_8(fft_module->sample_out_11_1);
    approx_result[12][0] = sign_extend_q8_8(fft_module->sample_out_12_0);
    approx_result[12][1] = sign_extend_q8_8(fft_module->sample_out_12_1);
    approx_result[13][0] = sign_extend_q8_8(fft_module->sample_out_13_0);
    approx_result[13][1] = sign_extend_q8_8(fft_module->sample_out_13_1);
    approx_result[14][0] = sign_extend_q8_8(fft_module->sample_out_14_0);
    approx_result[14][1] = sign_extend_q8_8(fft_module->sample_out_14_1);
    approx_result[15][0] = sign_extend_q8_8(fft_module->sample_out_15_0);
    approx_result[15][1] = sign_extend_q8_8(fft_module->sample_out_15_1);

    approx_results.push_back(approx_result);

    fft_raw(input_values[i], exact_result);
    exact_results.push_back(exact_result);
  }
  delete fft_module;
}

int main() {
  const PatternSet input_values = generate_fft_input_patterns();
  export_kernel_pattern_files(input_values, std::filesystem::path("kernels"));

  double sum_percentage_error = 0.0;
  double sum_abs_error = 0.0;
  double sum_squared_error = 0.0;
  double sum_exact_energy = 0.0;
  std::vector<Matrix16x2> approx_result;
  std::vector<Matrix16x2> exact_result;
  approx_result.reserve(input_values.size());
  exact_result.reserve(input_values.size());
  test_fft(input_values, approx_result, exact_result);

  for (size_t i = 0; i < input_values.size(); ++i) {
    for (size_t j = 0; j < 16; ++j) {
      for (size_t k = 0; k < 2; ++k) {
        const long diff =
            static_cast<long>(approx_result[i][j][k]) - static_cast<long>(exact_result[i][j][k]);
        const double diff_double = static_cast<double>(diff);
        const double exact_double = static_cast<double>(exact_result[i][j][k]);
        sum_abs_error += std::abs(diff_double);
        sum_squared_error += diff_double * diff_double;
        sum_exact_energy += exact_double * exact_double;

        if (approx_result[i][j][k] != exact_result[i][j][k]) {
          if (exact_result[i][j][k] == 0 && approx_result[i][j][k] == 0) {
            continue; // Both are zero, treat as zero error
          }
          if (exact_result[i][j][k] == 0) {
            sum_percentage_error += 1.0; // Treat zero exact value as 100% error
            continue;
          }
          sum_percentage_error +=
              std::abs(static_cast<double>(diff)) / std::abs(static_cast<double>(exact_result[i][j][k]));
        }
      }
    }
  }

  const double output_count = static_cast<double>(input_values.size() * 16 * 2);
  const double mpe = sum_percentage_error / output_count;
  const double med = sum_abs_error / output_count;
  const double mse = sum_squared_error / output_count;
  const double nmed = med / static_cast<double>(1ULL << 16);
  const double nmse = sum_squared_error / std::max(sum_exact_energy, 1.0);
  std::cout << std::setprecision(17) << "MAPE: " << mpe << std::endl;
  std::cout << std::setprecision(17) << "NMED: " << nmed << std::endl;
  std::cout << std::setprecision(17) << "MSE: " << mse << std::endl;
  std::cout << std::setprecision(17) << "NMSE: " << nmse << std::endl;

  return 0;
}
