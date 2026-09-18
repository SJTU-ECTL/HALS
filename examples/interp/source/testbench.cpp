#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include "interp.h"
#include "Vinterp_wrapper.h"

static double compute_mape(int64_t exact, int64_t approx) {
    if (exact == 0) {
        return (approx == 0) ? 0.0 : 1.0;
    }
    return std::abs(static_cast<double>(exact - approx)) / std::abs(static_cast<double>(exact));
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);

    std::cout << "Generating " << SIM_PATTERN_N << " input patterns..." << std::endl;
    auto input_values = generate_interp_input_patterns();
    std::cout << "Generated " << input_values.size() << " patterns." << std::endl;

    std::cout << "Exporting kernel pattern files..." << std::endl;
    // Use working directory + kernels, since we run from examples/interp/
    std::filesystem::path kernel_dir = std::filesystem::absolute("kernels");
    export_kernel_pattern_files(input_values, kernel_dir);
    std::cout << "Kernel pattern files exported to " << kernel_dir << std::endl;

    auto top = std::make_unique<Vinterp_wrapper>();

    double total_mape = 0.0;
    double total_squared_error = 0.0;
    double total_exact_energy = 0.0;
    size_t mismatches = 0;
    double max_mape = 0.0;
    double sum_mape[4] = {0, 0, 0, 0};

    for (size_t i = 0; i < SIM_PATTERN_N; ++i) {
        top->idata_0 = input_values[i][0];
        top->idata_1 = input_values[i][1];
        top->idata_2 = input_values[i][2];
        top->idata_3 = input_values[i][3];
        top->idata_4 = input_values[i][4];
        top->idata_5 = input_values[i][5];
        top->idata_6 = input_values[i][6];
        top->idata_7 = input_values[i][7];
        top->eval();

        int64_t approx[4];
        approx[0] = static_cast<int64_t>(top->odata_0);
        approx[1] = static_cast<int64_t>(top->odata_1);
        approx[2] = static_cast<int64_t>(top->odata_2);
        approx[3] = static_cast<int64_t>(top->odata_3);

        Matrix4 exact;
        interp_raw(input_values[i], exact);

        for (int k = 0; k < 4; ++k) {
            double mape = compute_mape(exact[k], approx[k]);
            const double diff = static_cast<double>(approx[k]) - static_cast<double>(exact[k]);
            const double exact_value = static_cast<double>(exact[k]);
            sum_mape[k] += mape;
            total_mape += mape;
            total_squared_error += diff * diff;
            total_exact_energy += exact_value * exact_value;
            if (mape > max_mape) max_mape = mape;
            if (exact[k] != approx[k]) mismatches++;
        }
    }

    double avg_mape = total_mape / (SIM_PATTERN_N * 4);
    double nmse = total_squared_error / std::max(total_exact_energy, 1.0);
    std::cout << std::scientific << std::setprecision(6);
    std::cout << "patterns   = " << SIM_PATTERN_N << std::endl;
    std::cout << "mismatches = " << mismatches << std::endl;
    std::cout << "avg MAPE   = " << avg_mape << std::endl;
    std::cout << "max MAPE   = " << max_mape << std::endl;
    std::cout << "NMSE       = " << nmse << std::endl;
    for (int k = 0; k < 4; ++k) {
        std::cout << "odata[" << k << "] avg MAPE = " << sum_mape[k] / SIM_PATTERN_N << std::endl;
    }

    return (mismatches > 0) ? 1 : 0;
}
