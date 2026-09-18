#include <algorithm>
#include <filesystem>
#include <cmath>
#include <iomanip>
#include <iostream>

#include "Vsyr2k_wrapper.h"
#include "syr2k.h"

int main() {
  const auto input_values = generate_syr2k_input_patterns();
  export_kernel_pattern_files(input_values, std::filesystem::path("kernels"));
  Vsyr2k_wrapper dut;
  size_t mismatches = 0;
  uint64_t abs_error_sum = 0;
  double squared_error_sum = 0.0;
  double exact_energy_sum = 0.0;
  double mape_sum = 0.0;
  for (const auto &p : input_values) {
    dut.C_in = static_cast<uint16_t>(p.C_in);
    dut.A_jk = static_cast<uint16_t>(p.A_jk);
    dut.B_ik = static_cast<uint16_t>(p.B_ik);
    dut.B_jk = static_cast<uint16_t>(p.B_jk);
    dut.A_ik = static_cast<uint16_t>(p.A_ik);
    dut.alpha = SYR2K_ALPHA;
    dut.beta = SYR2K_BETA;
    dut.eval();
    uint32_t term2 = kernel_syr2k_1(p.A_jk, p.B_ik, p.B_jk, p.A_ik);
    uint32_t expected = kernel_syr2k_2(SYR2K_BETA, p.C_in, SYR2K_ALPHA, term2);
    uint32_t actual = static_cast<uint16_t>(dut.C_next);
    const uint32_t abs_error = (actual > expected) ? (actual - expected) : (expected - actual);
    const double diff = static_cast<double>(actual) - static_cast<double>(expected);
    const double exact = static_cast<double>(expected);
    if (actual != expected) mismatches++;
    abs_error_sum += abs_error;
    squared_error_sum += diff * diff;
    exact_energy_sum += exact * exact;
    mape_sum += mape_term(actual, expected);
  }
  const double nmse = squared_error_sum / std::max(exact_energy_sum, 1.0);
  std::cout << "syr2k testbench results:\n";
  std::cout << "  patterns   = " << input_values.size() << "\n";
  std::cout << "  mismatches = " << mismatches << "\n";
  std::cout << std::setprecision(17) << "  NMED       = "
            << (static_cast<double>(abs_error_sum) / input_values.size() / static_cast<double>((1u << 16) - 1u)) << "\n";
  std::cout << std::setprecision(17) << "  MAPE       = " << (mape_sum / input_values.size()) << "\n";
  std::cout << std::setprecision(17) << "  NMSE       = " << nmse << "\n";
  return mismatches ? 1 : 0;
}
