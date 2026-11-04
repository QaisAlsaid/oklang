#ifndef OK_UTILS_H
#define OK_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ok/ok_source.h"
#include "okdiag.h"
#include "okfwd.h"
#include "okstring.h"
#include "oktoken.h"

// TODO: remove to somewhere better
#define MAX_SAFE_INTEGER (9.0e15)
#define MIN_SAFE_INTEGER (-(MAX_SAFE_INTEGER))

void encode_int(uint8_t* p_bytes, const uint8_t p_bytes_count, const uint64_t p_value);
uint64_t decode_int(const uint8_t* p_bytes, const uint8_t p_bytes_count);

string asprint(allocators* p_alloc, const char* p_fmt, ...);

report_status report_at(bool* p_panic,
                        bool* p_had_error,
                        const bool p_is_eof,
                        const ok_source* p_source,
                        const token* p_token,
                        const report_severity p_severity,
                        const ok_string_view p_message);
report_status report_at_noted(bool* p_panic,
                              bool* p_had_error,
                              const bool p_is_eof,
                              const ok_source* p_source,
                              const token* p_token,
                              const report_severity p_severity,
                              const ok_string_view p_message,
                              const ok_note* p_note);

inline static hash_t fnv1a_hash_str(const ok_string_view p_string) {
  hash_t hash = 14695981039346656037ULL;
  for (uint64_t i = 0; i < ok_string_view_get_length(&p_string); ++i) {
    hash ^= (uint8_t)p_string.chars[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

inline static hash_t default_hash_str(const ok_string_view p_str) {
  return fnv1a_hash_str(p_str);
}

inline static hash_t hash_combine(const hash_t p_first, hash_t p_second) {
  return p_first ^ (p_second + 0x9e3779b9 + (p_first << 6) + (p_first >> 2));
}

inline static hash_t splitmix64(hash_t p_x) {
  p_x += UINT64_C(0x9e3779b97f4a7c15);
  p_x = (p_x ^ (p_x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  p_x = (p_x ^ (p_x >> 27)) * UINT64_C(0x94d049bb133111eb);
  return p_x ^ (p_x >> 31);
}

#endif // OK_UTILS_H
