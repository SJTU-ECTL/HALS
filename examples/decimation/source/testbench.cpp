#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#include "Vdecim_wrapper.h"
#include "decimation.h"

inline int32_t sign_extend_16(uint32_t raw) {
  return static_cast<int32_t>(static_cast<int16_t>(raw));
}

int main() {
  const PatternSet input_values = generate_decimation_input_patterns();
  export_kernel_pattern_files(input_values, std::filesystem::path("kernels"));

  Vdecim_wrapper *dut = new Vdecim_wrapper;
  size_t total_patterns = input_values.size();
  int64_t total_abs_error = 0;
  int64_t max_abs_error = 0;
  double total_percentage_error = 0.0;
  double total_squared_error = 0.0;
  double total_exact_energy = 0.0;
  size_t mismatch_count = 0;

  // Reset the DUT
  dut->clk = 0;
  dut->rst = 1;
  dut->data_in = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
  dut->clk = 0;
  dut->rst = 0;
  dut->eval();

  // C++ pipeline state for history-based comparison
  DecimPipelineState cpp_state;

  for (size_t i = 0; i < total_patterns; ++i) {
    int32_t input_value = input_values[i];

    // Apply input and toggle clock (posedge)
    dut->data_in = input_value;
    dut->clk = 1;
    dut->eval();

    int32_t actual = sign_extend_16(dut->data_out);

    // C++ reference: one clock step with history
    int32_t expected = decim_pipeline_step(input_value, cpp_state);

    // Toggle clock low for next cycle
    dut->clk = 0;
    dut->eval();

    int64_t abs_error = std::llabs(static_cast<int64_t>(actual) - expected);
    const double diff = static_cast<double>(actual) - static_cast<double>(expected);
    const double exact = static_cast<double>(expected);
    if (expected == 0) {
      total_percentage_error += (actual == 0) ? 0.0 : 1.0;
    } else {
      total_percentage_error +=
          static_cast<double>(abs_error) / std::abs(static_cast<double>(expected));
    }

    total_abs_error += abs_error;
    total_squared_error += diff * diff;
    total_exact_energy += exact * exact;
    max_abs_error = std::max(max_abs_error, abs_error);
    if (abs_error != 0) {
      mismatch_count += 1;
    }
  }
  const double nmse = total_squared_error / std::max(total_exact_energy, 1.0);

  std::cout << "Decimation testbench results:\n";
  std::cout << "  patterns      = " << total_patterns << "\n";
  std::cout << "  mismatches    = " << mismatch_count << "\n";
  std::cout << "  average error = " << (static_cast<double>(total_abs_error) / total_patterns) << "\n";
  std::cout << std::setprecision(17)
            << "  NMED          = "
            << (static_cast<double>(total_abs_error) / total_patterns /
                static_cast<double>((1u << (DECIM_WORD_WIDTH - 1)) - 1u)) << "\n";
  std::cout << std::setprecision(17)
            << "  MAPE          = " << (total_percentage_error / total_patterns) << "\n";
  std::cout << std::setprecision(17)
            << "  NMSE          = " << nmse << "\n";
  std::cout << std::setprecision(17)
  std::cout << "  max error     = " << max_abs_error << "\n";

  dut->final();
  delete dut;
  return mismatch_count ? 1 : 0;
}
