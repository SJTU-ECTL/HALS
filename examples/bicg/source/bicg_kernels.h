#pragma once
#include <cstdint>

inline uint32_t kernel_bicg_s_acc(uint32_t s_acc, uint32_t Aij, uint32_t ri) {
  return q8_clip16(s_acc + q8_mul(Aij, ri));
}

inline uint32_t kernel_bicg_q_acc(uint32_t q_acc, uint32_t Aij, uint32_t pj) {
  return q8_clip16(q_acc + q8_mul(Aij, pj));
}
