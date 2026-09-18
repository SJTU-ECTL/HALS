#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#include "Vcholesky_wrapper.h"
#include "cholesky.h"

static void tick(Vcholesky_wrapper *dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
  dut->clk = 0;
  dut->eval();
}

int main() {
  const PatternSet input_values = generate_cholesky_input_patterns();
  export_kernel_pattern_files(input_values, std::filesystem::path("kernels"));

  Vcholesky_wrapper *dut = new Vcholesky_wrapper;
  size_t total_patterns = input_values.size();
  constexpr double nmed_denominator = static_cast<double>(1ULL << 16);
  int64_t total_abs_error = 0;
  int64_t max_abs_error = 0;
  double total_percentage_error = 0.0;
  double total_squared_error = 0.0;
  double total_exact_energy = 0.0;
  size_t mismatch_count = 0;

  dut->clk = 0;
  dut->rst = 1;
  dut->start = 0;
  tick(dut);
  dut->rst = 0;
  tick(dut);

  for (size_t i = 0; i < total_patterns; ++i) {
    const auto &A = input_values[i];
    dut->a00 = static_cast<uint16_t>(A[0]);
    dut->a10 = static_cast<uint16_t>(A[1]);
    dut->a20 = static_cast<uint16_t>(A[2]);
    dut->a11 = static_cast<uint16_t>(A[4]);
    dut->a21 = static_cast<uint16_t>(A[5]);
    dut->a22 = static_cast<uint16_t>(A[8]);
    dut->start = 1;
    tick(dut);
    dut->start = 0;
    for (int cycle = 0; cycle < 8 && !dut->valid; ++cycle) {
      tick(dut);
    }
    if (!dut->valid) {
      std::cerr << "Timed out waiting for cholesky valid at pattern " << i << "\n";
      delete dut;
      return 2;
    }

    uint32_t expected[9];
    cholesky_pipeline_exact(A.data(), expected);

    uint32_t actual[9] = {
        static_cast<uint16_t>(dut->l00), static_cast<uint16_t>(dut->l10),
        static_cast<uint16_t>(dut->l20), static_cast<uint16_t>(dut->l03),
        static_cast<uint16_t>(dut->l11), static_cast<uint16_t>(dut->l21),
        static_cast<uint16_t>(dut->l06), static_cast<uint16_t>(dut->l07),
        static_cast<uint16_t>(dut->l22)
    };

    for (int j = 0; j < 9; ++j) {
      int64_t abs_error = std::llabs(static_cast<int64_t>(actual[j]) - expected[j]);
      const double diff = static_cast<double>(actual[j]) - static_cast<double>(expected[j]);
      const double exact = static_cast<double>(expected[j]);
      if (expected[j] == 0) {
        total_percentage_error += (actual[j] == 0) ? 0.0 : 1.0;
      } else {
        total_percentage_error +=
            static_cast<double>(abs_error) / std::abs(static_cast<double>(expected[j]));
      }
      total_abs_error += abs_error;
      total_squared_error += diff * diff;
      total_exact_energy += exact * exact;
      max_abs_error = std::max(max_abs_error, abs_error);
      if (abs_error != 0) {
        mismatch_count += 1;
      }
    }
  }
  const double nmse = total_squared_error / std::max(total_exact_energy, 1.0);

  std::cout << "Cholesky testbench results:\n";
  std::cout << "  patterns      = " << total_patterns << "\n";
  std::cout << "  mismatches    = " << mismatch_count << "\n";
  std::cout << "  average error = " << (static_cast<double>(total_abs_error) / (total_patterns * 9)) << "\n";
  std::cout << std::setprecision(17)
            << "  NMED          = "
            << (static_cast<double>(total_abs_error) /
                (static_cast<double>(total_patterns * 9) * nmed_denominator))
            << "\n";
  std::cout << std::setprecision(17)
            << "  MAPE          = "
            << (total_percentage_error / static_cast<double>(total_patterns * 9))
            << "\n";
  std::cout << std::setprecision(17)
            << "  NMSE          = " << nmse << "\n";
  std::cout << std::setprecision(17)
  std::cout << "  max error     = " << max_abs_error << "\n";

  dut->final();
  delete dut;
  return mismatch_count ? 1 : 0;
}
