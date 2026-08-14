#include "utils.h"

void encode_int(uint8_t* p_bytes, uint8_t p_bytes_count, uint64_t p_value) {
  for (uint8_t i = 0; i < p_bytes_count; ++i) {
    p_bytes[i] = ((p_value) >> (8 * i) & 0xff);
  }
}

uint64_t decode_int(uint8_t* p_bytes, uint8_t p_bytes_count) {
  uint64_t result = 0;
  for (uint8_t i = 0; i < p_bytes_count; ++i) {
    result |= p_bytes[i] << (8 * i);
  }
  return result;
}


