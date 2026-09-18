#pragma once
#include <cstdint>

inline void kernel_gesummv_acc(uint32_t tmp_acc, uint32_t y_acc, uint32_t Aij, uint32_t Bij, uint32_t xj,
                               uint32_t &tmp_next, uint32_t &y_next) {
  tmp_next = static_cast<uint16_t>(tmp_acc + q8_mul(Aij, xj));
  y_next = static_cast<uint16_t>(y_acc + q8_mul(Bij, xj));
}

inline uint32_t kernel_gesummv_combine(uint32_t alpha, uint32_t beta, uint32_t tmp_final, uint32_t y_final) {
  return static_cast<uint16_t>(q8_mul(alpha, tmp_final) + q8_mul(beta, y_final));
}
