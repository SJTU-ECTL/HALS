#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>

#include "Vbicg_wrapper.h"
#include "bicg.h"

static void tick(Vbicg_wrapper &dut) {
  dut.clk = 0;
  dut.eval();
  dut.clk = 1;
  dut.eval();
}

int main() {
  const auto input_values = generate_bicg_input_patterns();
  export_bicg_kernel_pattern_files(input_values, std::filesystem::path("kernels"));

  size_t valid_count = 0;
  size_t expected_count = 0;
  size_t mismatches = 0;
  uint64_t abs_error_sum = 0;
  double squared_error_sum = 0.0;
  double exact_energy_sum = 0.0;
  double mape_sum = 0.0;

  Vbicg_wrapper dut;
  for (const auto &matrix : input_values) {
    const BicgOutputs expected = bicg_exact_outputs(matrix);
    expected_count += expected.size();

    dut.rst = 1;
    dut.Aij = 0;
    dut.pj = 0;
    dut.ri = 0;
    tick(dut);
    dut.rst = 0;

    size_t out_index = 0;
    for (int i = 0; i < BICG_N; ++i) {
      for (int j = 0; j < BICG_M; ++j) {
        dut.Aij = static_cast<uint16_t>(matrix.a[i][j]);
        dut.pj = static_cast<uint16_t>(matrix.p[j]);
        dut.ri = static_cast<uint16_t>(matrix.r[i]);
        tick(dut);
        if (dut.valid_out) {
          const uint32_t actual = static_cast<uint16_t>(dut.out);
          const uint32_t ref = expected[out_index++];
          const uint32_t abs_error = (actual > ref) ? (actual - ref) : (ref - actual);
          const double diff = static_cast<double>(actual) - static_cast<double>(ref);
          if (actual != ref) mismatches++;
          abs_error_sum += abs_error;
          squared_error_sum += diff * diff;
          exact_energy_sum += static_cast<double>(ref) * static_cast<double>(ref);
          mape_sum += mape_term(actual, ref);
          valid_count++;
        }
      }
    }
    for (int j = 0; j < BICG_M; ++j) {
      dut.Aij = 0;
      dut.pj = 0;
      dut.ri = 0;
      tick(dut);
      if (dut.valid_out) {
        const uint32_t actual = static_cast<uint16_t>(dut.out);
        const uint32_t ref = expected[out_index++];
        const uint32_t abs_error = (actual > ref) ? (actual - ref) : (ref - actual);
        const double diff = static_cast<double>(actual) - static_cast<double>(ref);
        if (actual != ref) mismatches++;
        abs_error_sum += abs_error;
        squared_error_sum += diff * diff;
        exact_energy_sum += static_cast<double>(ref) * static_cast<double>(ref);
        mape_sum += mape_term(actual, ref);
        valid_count++;
      }
    }
  }

  const double nmse = squared_error_sum / std::max(exact_energy_sum, 1.0);
  std::cout << "bicg testbench results:\n";
  std::cout << "  cases         = " << input_values.size() << "\n";
  std::cout << "  expected outs = " << expected_count << "\n";
  std::cout << "  valid outputs = " << valid_count << "\n";
  std::cout << "  mismatches    = " << mismatches << "\n";
  std::cout << std::setprecision(17) << "  NMED          = "
            << (static_cast<double>(abs_error_sum) / valid_count / static_cast<double>((1u << 16) - 1u)) << "\n";
  std::cout << std::setprecision(17) << "  MAPE          = " << (mape_sum / valid_count) << "\n";
  std::cout << std::setprecision(17) << "  NMSE          = " << nmse << "\n";
  return mismatches ? 1 : 0;
}
