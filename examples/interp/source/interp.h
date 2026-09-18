#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define TAPS 8
#define SIM_PATTERN_N 65536

using Matrix8 = std::array<int16_t, 8>;
using Matrix4 = std::array<int64_t, 4>;

// fixed coefficients from interp reference (Q14.2 format)
constexpr int16_t COEFF1_0 = 0xFFBA;  // -70
constexpr int16_t COEFF1_1 = 0x0245;  // 581
constexpr int16_t COEFF1_2 = 0xF622;  // -2526
constexpr int16_t COEFF1_3 = 0x24D0;  // 9424
constexpr int16_t COEFF1_4 = 0x6E60;  // 28256
constexpr int16_t COEFF1_5 = 0xF236;  // -3530
constexpr int16_t COEFF1_6 = 0x02C5;  // 709
constexpr int16_t COEFF1_7 = 0xFFB0;  // -80

constexpr int16_t COEFF2_0 = 0xFF98;  // -104
constexpr int16_t COEFF2_1 = 0x037A;  // 890
constexpr int16_t COEFF2_2 = 0xEFF8;  // -4104
constexpr int16_t COEFF2_3 = 0x4CF3;  // 19699
constexpr int16_t COEFF2_4 = 0x4CF3;  // 19699
constexpr int16_t COEFF2_5 = 0xEFF8;  // -4104
constexpr int16_t COEFF2_6 = 0x037A;  // 890
constexpr int16_t COEFF2_7 = 0xFF98;  // -104

constexpr int16_t COEFF3_0 = 0xFFB0;  // -80
constexpr int16_t COEFF3_1 = 0x02C5;  // 709
constexpr int16_t COEFF3_2 = 0xF236;  // -3530
constexpr int16_t COEFF3_3 = 0x6E60;  // 28256
constexpr int16_t COEFF3_4 = 0x24D0;  // 9424
constexpr int16_t COEFF3_5 = 0xF622;  // -2526
constexpr int16_t COEFF3_6 = 0x0245;  // 581
constexpr int16_t COEFF3_7 = 0xFFBA;  // -70

constexpr int16_t COEFF4_0 = 0xFFFF;  // -1
constexpr int16_t COEFF4_1 = 0x0000;  // 0
constexpr int16_t COEFF4_2 = 0xFFFF;  // -1
constexpr int16_t COEFF4_3 = 0x8000;  // -32768
constexpr int16_t COEFF4_4 = 0xFFFF;  // -1
constexpr int16_t COEFF4_5 = 0x0000;  // 0
constexpr int16_t COEFF4_6 = 0xFFFF;  // -1

// exact interpolation reference with FIXED coefficients
inline void interp_raw(const Matrix8 &idata, Matrix4 &odata) {
    int64_t sop1 = 0, sop2 = 0, sop3 = 0, sop4 = 0;
    sop1 += static_cast<int64_t>(idata[0]) * static_cast<int64_t>(COEFF1_0);
    sop1 += static_cast<int64_t>(idata[1]) * static_cast<int64_t>(COEFF1_1);
    sop1 += static_cast<int64_t>(idata[2]) * static_cast<int64_t>(COEFF1_2);
    sop1 += static_cast<int64_t>(idata[3]) * static_cast<int64_t>(COEFF1_3);
    sop1 += static_cast<int64_t>(idata[4]) * static_cast<int64_t>(COEFF1_4);
    sop1 += static_cast<int64_t>(idata[5]) * static_cast<int64_t>(COEFF1_5);
    sop1 += static_cast<int64_t>(idata[6]) * static_cast<int64_t>(COEFF1_6);
    sop1 += static_cast<int64_t>(idata[7]) * static_cast<int64_t>(COEFF1_7);
    sop2 += static_cast<int64_t>(idata[0]) * static_cast<int64_t>(COEFF2_0);
    sop2 += static_cast<int64_t>(idata[1]) * static_cast<int64_t>(COEFF2_1);
    sop2 += static_cast<int64_t>(idata[2]) * static_cast<int64_t>(COEFF2_2);
    sop2 += static_cast<int64_t>(idata[3]) * static_cast<int64_t>(COEFF2_3);
    sop2 += static_cast<int64_t>(idata[4]) * static_cast<int64_t>(COEFF2_4);
    sop2 += static_cast<int64_t>(idata[5]) * static_cast<int64_t>(COEFF2_5);
    sop2 += static_cast<int64_t>(idata[6]) * static_cast<int64_t>(COEFF2_6);
    sop2 += static_cast<int64_t>(idata[7]) * static_cast<int64_t>(COEFF2_7);
    sop3 += static_cast<int64_t>(idata[0]) * static_cast<int64_t>(COEFF3_0);
    sop3 += static_cast<int64_t>(idata[1]) * static_cast<int64_t>(COEFF3_1);
    sop3 += static_cast<int64_t>(idata[2]) * static_cast<int64_t>(COEFF3_2);
    sop3 += static_cast<int64_t>(idata[3]) * static_cast<int64_t>(COEFF3_3);
    sop3 += static_cast<int64_t>(idata[4]) * static_cast<int64_t>(COEFF3_4);
    sop3 += static_cast<int64_t>(idata[5]) * static_cast<int64_t>(COEFF3_5);
    sop3 += static_cast<int64_t>(idata[6]) * static_cast<int64_t>(COEFF3_6);
    sop3 += static_cast<int64_t>(idata[7]) * static_cast<int64_t>(COEFF3_7);
    sop4 += static_cast<int64_t>(idata[0]) * static_cast<int64_t>(COEFF4_0);
    sop4 += static_cast<int64_t>(idata[1]) * static_cast<int64_t>(COEFF4_1);
    sop4 += static_cast<int64_t>(idata[2]) * static_cast<int64_t>(COEFF4_2);
    sop4 += static_cast<int64_t>(idata[3]) * static_cast<int64_t>(COEFF4_3);
    sop4 += static_cast<int64_t>(idata[4]) * static_cast<int64_t>(COEFF4_4);
    sop4 += static_cast<int64_t>(idata[5]) * static_cast<int64_t>(COEFF4_5);
    sop4 += static_cast<int64_t>(idata[6]) * static_cast<int64_t>(COEFF4_6);
    odata[0] = sop1;
    odata[1] = sop2;
    odata[2] = sop3;
    odata[3] = sop4;
}

// generate random input patterns with safe range to avoid 35-bit overflow
inline std::vector<Matrix8> generate_interp_input_patterns() {
    std::vector<Matrix8> input_values;
    input_values.reserve(SIM_PATTERN_N);
    std::mt19937 gen(0);
    std::uniform_int_distribution<int16_t> dist(-16384, 16383);

    for (size_t i = 0; i < SIM_PATTERN_N; ++i) {
        Matrix8 idata;
        for (size_t j = 0; j < 8; ++j) {
            idata[j] = dist(gen);
        }
        input_values.push_back(idata);
    }
    return input_values;
}

// ------------------- pattern file export utilities -------------------

inline void append_scalar_bits(std::string &line, int64_t value, int width) {
    const uint64_t word = static_cast<uint64_t>(value);
    for (int bit = 0; bit < width; ++bit) {
        line.push_back(((word >> bit) & 1ULL) ? '1' : '0');
    }
}

// export interp kernel pattern files for ALS
// Each kernel takes 8 idata inputs (128 bits), produces 1 output (35 bits)
inline void export_kernel_pattern_files(
    const std::vector<Matrix8> &input_values,
    const std::filesystem::path &kernel_dir) {
    std::filesystem::create_directories(kernel_dir);

    const std::array<const char *, 7> kernel_names = {
        "interp_k1_p0", "interp_k1_p1", "interp_k2_p0", "interp_k2_p1",
        "interp_k3_p0", "interp_k3_p1", "interp_k4"};
    const std::array<int, 7> first_ports = {0, 4, 0, 4, 0, 4, 0};
    const std::array<int, 7> port_counts = {4, 4, 4, 4, 4, 4, 8};
    const int n_output_bits = 35;
    const size_t n_frames = input_values.size();

    for (size_t kernel = 0; kernel < kernel_names.size(); ++kernel) {
        const std::filesystem::path als_path =
            kernel_dir / (std::string(kernel_names[kernel]) + ".als.pattern");
        const std::filesystem::path packed_path =
            kernel_dir / (std::string(kernel_names[kernel]) + ".als64.pattern");

        std::ofstream als_file(als_path);
        std::ofstream packed_file(packed_path);

        for (size_t frame = 0; frame < n_frames; ++frame) {
            const Matrix8 &idata = input_values[frame];
            Matrix4 exact_odata;
            interp_raw(idata, exact_odata);
            const int64_t partials[] = {
                static_cast<int64_t>(idata[0]) * COEFF1_0 +
                    static_cast<int64_t>(idata[1]) * COEFF1_1 +
                    static_cast<int64_t>(idata[2]) * COEFF1_2 +
                    static_cast<int64_t>(idata[3]) * COEFF1_3,
                static_cast<int64_t>(idata[4]) * COEFF1_4 +
                    static_cast<int64_t>(idata[5]) * COEFF1_5 +
                    static_cast<int64_t>(idata[6]) * COEFF1_6 +
                    static_cast<int64_t>(idata[7]) * COEFF1_7,
                static_cast<int64_t>(idata[0]) * COEFF2_0 +
                    static_cast<int64_t>(idata[1]) * COEFF2_1 +
                    static_cast<int64_t>(idata[2]) * COEFF2_2 +
                    static_cast<int64_t>(idata[3]) * COEFF2_3,
                static_cast<int64_t>(idata[4]) * COEFF2_4 +
                    static_cast<int64_t>(idata[5]) * COEFF2_5 +
                    static_cast<int64_t>(idata[6]) * COEFF2_6 +
                    static_cast<int64_t>(idata[7]) * COEFF2_7,
                static_cast<int64_t>(idata[0]) * COEFF3_0 +
                    static_cast<int64_t>(idata[1]) * COEFF3_1 +
                    static_cast<int64_t>(idata[2]) * COEFF3_2 +
                    static_cast<int64_t>(idata[3]) * COEFF3_3,
                static_cast<int64_t>(idata[4]) * COEFF3_4 +
                    static_cast<int64_t>(idata[5]) * COEFF3_5 +
                    static_cast<int64_t>(idata[6]) * COEFF3_6 +
                    static_cast<int64_t>(idata[7]) * COEFF3_7,
            };
            int64_t expected_out = (kernel < 6) ? partials[kernel] : exact_odata[3];

            std::string line;
            for (int port = 0; port < port_counts[kernel]; ++port) {
                const int input_index = first_ports[kernel] + port;
                append_scalar_bits(line, static_cast<int64_t>(idata[input_index]), 16);
            }
            append_scalar_bits(line, expected_out, 35);
            als_file << line << '\n';

            // packed 64-bit format for .als64.pattern
            std::string all_bits;
            all_bits.reserve(port_counts[kernel] * 16 + n_output_bits);
            for (int port = 0; port < port_counts[kernel]; ++port) {
                const int input_index = first_ports[kernel] + port;
                uint16_t val = static_cast<uint16_t>(static_cast<int64_t>(idata[input_index]));
                for (int b = 0; b < 16; ++b)
                    all_bits.push_back((val & (1U << b)) ? '1' : '0');
            }
            for (int b = 0; b < 35; ++b)
                all_bits.push_back(((expected_out >> b) & 1LL) ? '1' : '0');

            packed_file << kernel_names[kernel] << " ";
            uint64_t accum = 0;
            int bit_pos = 0;
            for (size_t i = 0; i < all_bits.size(); ++i) {
                if (all_bits[i] == '1') accum |= (1ULL << bit_pos);
                bit_pos++;
                if (bit_pos == 64) {
                    packed_file << "0x" << std::hex << std::setw(16) << std::setfill('0')
                                << accum << std::dec << " ";
                    accum = 0;
                    bit_pos = 0;
                }
            }
            if (bit_pos > 0) {
                packed_file << "0x" << std::hex << std::setw(16) << std::setfill('0')
                            << accum << std::dec;
            }
            packed_file << '\n';
        }
    }
}
