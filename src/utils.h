#ifndef OK_UTILS_H
#define OK_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "source.h"
#include "token.h"

void encode_int(uint8_t* p_bytes, const uint8_t p_bytes_count, const uint64_t p_value);
uint64_t decode_int(const uint8_t* p_bytes, const uint8_t p_bytes_count);

// immutable string with length, and dynamic allocaton info.
typedef struct {
  uint64_t info;     // length + one bit for is_dynamic + 7 free bits. (don't attempt to read length from here)
  const char* chars; // immutable
} string;

#define UINT56_MAX ((1ul << 56) - 1)
#define STRING_MAX UINT56_MAX
#define STRING_CALCULATE_LENGTH UINT56_MAX + 1
#define STRING_IGNORE_LENGTH UINT56_MAX + 2
#define STRING_FLAGS_TOP STRING_IGNORE_LENGTH

bool string_init(string* p_string, const char* p_chars, uint64_t p_length, const bool p_is_dynamic);
void string_deinit(string* p_string);
uint64_t string_get_length(const string* p_string);
bool string_is_dynamic(const string* p_string);
string create_string(const char* p_chars, const uint64_t p_length, const bool p_is_dynamic);
string copy_string(string p_string);

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
                        const string p_message);
report_status report_at_noted(bool* p_panic,
                              bool* p_had_error,
                              const bool p_is_eof,
                              const source* p_source,
                              const token* p_token,
                              const report_severity p_severity,
                              const string p_message,
                              const string p_note);

inline static size_t fnv1a_hash_str(const char* p_str, size_t p_hash) {
  return (*p_str == 0) ? p_hash : fnv1a_hash_str(p_str + 1, (p_hash ^ (size_t)*p_str) * 1099511628211ULL);
}

inline static size_t default_hash_str(const char* p_str) {
  return fnv1a_hash_str(p_str, 14695981039346656037ULL);
}

#endif // OK_UTILS_H
