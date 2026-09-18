#include "Vconv3x3_wrapper.h"
#include "verilated.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

#include "conv3x3.h"

void test_conv3(const Matrix3x3Set &a, const Matrix3x3Set &b, std::vector<int32_t> &approx_result,
                std::vector<int32_t> &exact_result) {
  Vconv3x3_wrapper *conv3_module = new Vconv3x3_wrapper;

  for (size_t i = 0; i < a.size(); ++i) {
    conv3_module->a0 = a[i][0][0];
    conv3_module->a1 = a[i][0][1];
    conv3_module->a2 = a[i][0][2];
    conv3_module->a3 = a[i][1][0];
    conv3_module->a4 = a[i][1][1];
    conv3_module->a5 = a[i][1][2];
    conv3_module->a6 = a[i][2][0];
    conv3_module->a7 = a[i][2][1];
    conv3_module->a8 = a[i][2][2];
    conv3_module->b0 = b[i][0][0];
    conv3_module->b1 = b[i][0][1];
    conv3_module->b2 = b[i][0][2];
    conv3_module->b3 = b[i][1][0];
    conv3_module->b4 = b[i][1][1];
    conv3_module->b5 = b[i][1][2];
    conv3_module->b6 = b[i][2][0];
    conv3_module->b7 = b[i][2][1];
    conv3_module->b8 = b[i][2][2];

    conv3_module->eval();
    approx_result.push_back(conv3_module->result);
    exact_result.push_back(static_cast<int32_t>(conv3(a[i], b[i])));
  }

  delete conv3_module;
}

int main() {
  const Conv3PatternSet patterns = generate_conv3_input_patterns();
  export_conv3_kernel_patterns(patterns, std::filesystem::path("kernels"));

  unsigned long sum_error = 0;
  double sum_percentage_error = 0.0;
  double sum_squared_error = 0.0;
  double sum_exact_energy = 0.0;
  std::vector<int32_t> approx_result;
  std::vector<int32_t> exact_result;
  approx_result.reserve(patterns.a.size());
  exact_result.reserve(patterns.a.size());

  test_conv3(patterns.a, patterns.b, approx_result, exact_result);

  for (size_t i = 0; i < patterns.a.size(); ++i) {
    const double exact_double = static_cast<double>(exact_result[i]);
    sum_exact_energy += exact_double * exact_double;
    if (approx_result[i] != exact_result[i]) {
      const int32_t abs_error = std::abs(approx_result[i] - exact_result[i]);
      const double diff_double = static_cast<double>(approx_result[i]) - exact_double;
      sum_error += static_cast<unsigned long>(abs_error);
      sum_squared_error += diff_double * diff_double;
      if (exact_result[i] == 0) {
        sum_percentage_error += 1.0;
      } else {
        sum_percentage_error +=
            static_cast<double>(abs_error) / std::abs(static_cast<double>(exact_result[i]));
      }
    }
  }

  const double med =
      static_cast<double>(sum_error) / static_cast<double>(patterns.a.size());
  const double nmed = med / static_cast<double>((1u << 20) - 1u);
  const double mse = sum_squared_error / static_cast<double>(patterns.a.size());
  const double nmse = sum_squared_error / std::max(sum_exact_energy, 1.0);
  std::cout << std::setprecision(17) << "MED: " << med << std::endl;
  std::cout << std::setprecision(17) << "NMED: " << nmed << std::endl;
  std::cout << std::setprecision(17) << "MAPE: " << (sum_percentage_error / static_cast<double>(patterns.a.size())) << std::endl;
  std::cout << std::setprecision(17) << "MSE: " << mse << std::endl;
  std::cout << std::setprecision(17) << "NMSE: " << nmse << std::endl;

  return 0;
}
