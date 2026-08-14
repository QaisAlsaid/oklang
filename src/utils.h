#ifndef OK_UTILS_H
#define OK_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "source.h"
#include "token.h"

void encode_int(uint8_t* p_bytes, const uint8_t p_bytes_count, const uint64_t p_value);
uint64_t decode_int(const uint8_t* p_bytes, const uint8_t p_bytes_count);

// immutable string with length, and dynamic allocaton info.
typedef struct {
  uint64_t info; // length + one bit for is_dynamic + 7 free bits. (don't attempt to read length from here)  
  const char* chars; // immutable
} string;

#define UINT56_MAX ((1ul << 56) - 1)
#define STRING_CALCULATE_LENGTH UINT56_MAX

string string_create(const char* p_chars, const uint64_t p_length, const bool p_is_dynamic);
bool string_init(string* p_string, const char* p_chars, uint64_t p_length, const bool p_is_dynamic);
void string_deinit(string* p_string);
uint64_t string_get_length(const string* p_string);
bool string_is_dynamic(const string* p_string);

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

report_status report_at(bool* p_panic, bool* p_had_error, const bool p_is_eof, const source* p_source,
    const token* p_token, const report_severity p_severity, const string* p_message);
report_status report_at_noted(bool* p_panic, bool* p_had_error, const bool p_is_eof, const source* p_source,
    const token* p_token, const report_severity p_severity, const string* p_message, const string* p_note);

#endif // OK_UTILS_H
