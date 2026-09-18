#pragma once
#include <cstdint>

inline uint32_t kernel_atax_tmp_acc(uint32_t tmp_acc, uint32_t Aij, uint32_t xj) {
  return q8_clip16(tmp_acc + q8_mul(Aij, xj));
}

inline uint32_t kernel_atax_y_acc(uint32_t y_acc, uint32_t Aij, uint32_t tmp_final) {
  return q8_clip16(y_acc + q8_mul(Aij, tmp_final));
}
