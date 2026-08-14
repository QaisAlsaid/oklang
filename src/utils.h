#ifndef OK_UTILS_H
#define OK_UTILS_H

#include <stdint.h>
#include <stddef.h>

void encode_int(uint8_t* p_bytes, uint8_t p_bytes_count, uint64_t p_value);
uint64_t decode_int(uint8_t* p_bytes, uint8_t p_bytes_count);

#endif // OK_UTILS_H
