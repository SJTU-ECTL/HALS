#pragma once

#include <cstdint>

// ============================================================================
// Cholesky Decomposition: 3 kernel C++ functions (bit-exact with SystemC)
// For 3x3 matrix, fixed-point unsigned Q8.8 (16-bit)
// ============================================================================

// Integer sqrt lookup table (128 entries, Q8.8 format)
// Derived from square_root_lut.h: round(sqrt(i) * 256) for integer i=0..127
static const uint32_t cholesky_sqrt_lut[128] = {
      0,  256,  362,  443,  512,  573,  627,  678,
    724,  768,  809,  849,  887,  923,  958,  992,
   1024, 1056, 1086, 1116, 1145, 1173, 1201, 1228,
   1254, 1280, 1305, 1330, 1355, 1379, 1403, 1426,
   1449, 1472, 1494, 1517, 1539, 1560, 1582, 1603,
   1625, 1645, 1666, 1687, 1707, 1727, 1747, 1767,
   1787, 1807, 1826, 1845, 1864, 1883, 1902, 1920,
   1939, 1957, 1975, 1993, 2011, 2029, 2047, 2065,
   2082, 2100, 2117, 2134, 2151, 2168, 2185, 2202,
   2219, 2236, 2253, 2269, 2286, 2302, 2318, 2335,
   2351, 2367, 2383, 2399, 2415, 2431, 2447, 2462,
   2478, 2494, 2509, 2525, 2540, 2555, 2571, 2586,
   2601, 2616, 2631, 2646, 2661, 2676, 2690, 2705,
   2720, 2734, 2749, 2763, 2778, 2792, 2806, 2820,
   2834, 2848, 2862, 2876, 2890, 2904, 2918, 2932,
   2945, 2959, 2973, 2986, 3000, 3013, 3026, 3040,
};

inline uint32_t cholesky_sqrt(uint32_t x) {
    // x is Q8.8 fixed-point. Take integer part as LUT index.
    int index = (x >> 8) & 0x7F;
    if (index >= 128) index = 127;
    return cholesky_sqrt_lut[index];
}

inline uint32_t clamp_q8_residual(int64_t x) {
    return x <= 0 ? 0U : static_cast<uint32_t>(x);
}

// ============================================================================
// Kernel 0: Compute column 0 — L[0][0], L[1][0], L[2][0]
// ============================================================================

inline void kernel_cholesky_0(
    uint32_t a00, uint32_t a10, uint32_t a20,
    uint32_t &l00, uint32_t &l10, uint32_t &l20)
{
    l00 = cholesky_sqrt(a00);
    // Fixed-point division: (a << 8) / L[0][0] to maintain Q8.8
    l10 = (l00 != 0) ? static_cast<uint32_t>((static_cast<uint64_t>(a10) << 8) / l00) : 0;
    l20 = (l00 != 0) ? static_cast<uint32_t>((static_cast<uint64_t>(a20) << 8) / l00) : 0;
}

// ============================================================================
// Kernel 1: Compute column 1 — L[1][1], L[2][1]
// ============================================================================

inline void kernel_cholesky_1(
    uint32_t a11, uint32_t a21,
    uint32_t l10, uint32_t l20,
    uint32_t &l11, uint32_t &l21)
{
    uint32_t l10_sq = static_cast<uint32_t>((static_cast<uint64_t>(l10) * l10) >> 8);
    l11 = cholesky_sqrt(clamp_q8_residual(static_cast<int64_t>(a11) - l10_sq));

    uint32_t prod = static_cast<uint32_t>((static_cast<uint64_t>(l20) * l10) >> 8);
    const int64_t diff = static_cast<int64_t>(a21) - prod;
    l21 = (l11 != 0 && diff > 0) ? static_cast<uint32_t>((static_cast<uint64_t>(diff) << 8) / l11) : 0;
}

// ============================================================================
// Kernel 2: Compute column 2 — L[2][2]
// ============================================================================

inline void kernel_cholesky_2(
    uint32_t a22, uint32_t l20, uint32_t l21,
    uint32_t &l22)
{
    uint32_t l20_sq = static_cast<uint32_t>((static_cast<uint64_t>(l20) * l20) >> 8);
    uint32_t l21_sq = static_cast<uint32_t>((static_cast<uint64_t>(l21) * l21) >> 8);
    l22 = cholesky_sqrt(clamp_q8_residual(static_cast<int64_t>(a22) - l20_sq - l21_sq));
}

// ============================================================================
// Full Cholesky pipeline: runs kernels 0→1→2 sequentially
// ============================================================================

inline void cholesky_pipeline_exact(const uint32_t A[9], uint32_t L[9]) {
    uint32_t l00, l10, l20, l11, l21, l22;
    kernel_cholesky_0(A[0], A[1], A[2], l00, l10, l20);
    kernel_cholesky_1(A[4], A[5], l10, l20, l11, l21);
    kernel_cholesky_2(A[8], l20, l21, l22);

    // Fill L matrix (lower triangular, column-major order)
    L[0] = l00;  L[1] = l10;  L[2] = l20;
    L[3] = 0;    L[4] = l11;  L[5] = l21;
    L[6] = 0;    L[7] = 0;    L[8] = l22;
}
