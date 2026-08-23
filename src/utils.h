#ifndef OK_UTILS_H
#define OK_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "okstring.h"
#include "source.h"
#include "token.h"

void encode_int(uint8_t* p_bytes, const uint8_t p_bytes_count, const uint64_t p_value);
uint64_t decode_int(const uint8_t* p_bytes, const uint8_t p_bytes_count);

string asprint(const char* p_fmt, ...);

typedef enum {
  REPORT_SEVERITY_WARN,
  REPORT_SEVERITY_ERROR,
} report_severity;

typedef enum {
  REPORT_STATUS_OK,
  REPORT_STATUS_FAILED,
  REPORT_STATUS_SKIPPED,
} report_status;

report_status report_at(bool* p_panic,
                        bool* p_had_error,
                        const bool p_is_eof,
                        const source* p_source,
                        const token* p_token,
                        const report_severity p_severity,
                        const string_view p_message);
report_status report_at_noted(bool* p_panic,
                              bool* p_had_error,
                              const bool p_is_eof,
                              const source* p_source,
                              const token* p_token,
                              const report_severity p_severity,
                              const string_view p_message,
                              const string_view p_note);

inline static hash_t fnv1a_hash_str(const string_view p_str, hash_t p_hash) {
  for (uint64_t i = 0; i < p_str.info; ++i) {
    p_hash ^= (uint8_t)p_str.chars[i] * 1099511628211ULL;
  }
  return p_hash;
}

inline static hash_t default_hash_str(const string_view p_str) {
  return fnv1a_hash_str(p_str, 14695981039346656037ULL);
}

#endif // OK_UTILS_H
