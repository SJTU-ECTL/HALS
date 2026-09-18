#include <algorithm>
#include <filesystem>
#include <cmath>
#include <iomanip>
#include <iostream>

#include "Vgesummv_wrapper.h"
#include "gesummv.h"

int main() {
  const auto input_values = generate_gesummv_input_patterns();
  export_kernel_pattern_files(input_values, std::filesystem::path("kernels"));

  Vgesummv_wrapper dut;
  dut.clk = 0;
  dut.rst = 1;
  dut.Aij = 0;
  dut.Bij = 0;
  dut.xj = 0;
  dut.alpha = GESUMMV_ALPHA;
  dut.beta = GESUMMV_BETA;
  dut.eval();
  dut.clk = 1;
  dut.eval();
  dut.rst = 0;

  size_t valid_count = 0;
  size_t mismatches = 0;
  uint64_t abs_error_sum = 0;
  double squared_error_sum = 0.0;
  double exact_energy_sum = 0.0;
  double mape_sum = 0.0;
  for (size_t i = 0; i < input_values.size(); ++i) {
    dut.clk = 0;
    dut.Aij = static_cast<uint16_t>(input_values[i].a);
    dut.Bij = static_cast<uint16_t>(input_values[i].b);
    dut.xj = static_cast<uint16_t>(input_values[i].x);
    dut.alpha = GESUMMV_ALPHA;
    dut.beta = GESUMMV_BETA;
    dut.eval();
    dut.clk = 1;
    dut.eval();
    if (dut.valid_out) {
      uint32_t expected = gesummv_step_exact(input_values, i);
      uint32_t actual = static_cast<uint16_t>(dut.out);
      const uint32_t abs_error = (actual > expected) ? (actual - expected) : (expected - actual);
      const double diff = static_cast<double>(actual) - static_cast<double>(expected);
      const double exact = static_cast<double>(expected);
      if (actual != expected) mismatches++;
      abs_error_sum += abs_error;
      squared_error_sum += diff * diff;
      exact_energy_sum += exact * exact;
      mape_sum += mape_term(actual, expected);
      valid_count++;
    }
  }
  const double nmse = squared_error_sum / std::max(exact_energy_sum, 1.0);
  std::cout << "gesummv testbench results:\n";
  std::cout << "  patterns      = " << input_values.size() << "\n";
  std::cout << "  valid outputs = " << valid_count << "\n";
  std::cout << "  mismatches    = " << mismatches << "\n";
  std::cout << std::setprecision(17) << "  NMED          = "
            << (static_cast<double>(abs_error_sum) / valid_count / static_cast<double>((1u << 16) - 1u)) << "\n";
  std::cout << std::setprecision(17) << "  MAPE          = " << (mape_sum / valid_count) << "\n";
  std::cout << std::setprecision(17) << "  NMSE          = " << nmse << "\n";
  return mismatches ? 1 : 0;
}
