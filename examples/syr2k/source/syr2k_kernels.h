#pragma once
#include <cstdint>

inline uint32_t kernel_syr2k_1(uint32_t A_jk, uint32_t B_ik, uint32_t B_jk, uint32_t A_ik) {
  return q8_clip16(q8_mul(A_jk, B_ik) + q8_mul(B_jk, A_ik));
}

inline uint32_t kernel_syr2k_2(uint32_t beta, uint32_t C_in, uint32_t alpha, uint32_t term2) {
  return q8_clip16(q8_mul(beta, C_in) + q8_mul(alpha, term2));
}
