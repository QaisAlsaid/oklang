#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okmm.h"
#include "okutils.h"

void encode_int(uint8_t* p_bytes, const uint8_t p_bytes_count, const uint64_t p_value) {
  for (uint8_t i = 0; i < p_bytes_count; ++i) {
    p_bytes[i] = ((p_value) >> (8 * i) & 0xff);
  }
}

uint64_t decode_int(const uint8_t* p_bytes, const uint8_t p_bytes_count) {
  uint64_t result = 0;
  for (uint8_t i = 0; i < p_bytes_count; ++i) {
    result |= (uint64_t)p_bytes[i] << (8 * i);
  }
  return result;
}

string asprint(allocators* p_alloc, const char* p_fmt, ...) {
  // TODO this double copies make string accept taking ownership of already created buffer
  int n = 0;
  size_t size = 0;
  char* chars = NULL;
  va_list ap;

  va_start(ap, p_fmt);
  n = vsnprintf(chars, size, p_fmt, ap);
  va_end(ap);

  if (n < 0) {
    return create_static_string(NULL, 0);
  }

  size = (size_t)n + 1;
  chars = p_alloc->allocate(p_alloc, size);
  if (chars == NULL) {
    return create_static_string(NULL, 0);
  }

  va_start(ap, p_fmt);
  n = vsnprintf(chars, size, p_fmt, ap);
  va_end(ap);

  if (n < 0) {
    p_alloc->release(p_alloc, chars);
    return create_static_string(NULL, 0);
  }

  string str = create_dynamic_string(chars, n, p_alloc);
  p_alloc->release(p_alloc, chars);
  return str;
}

report_status report_at(bool* p_panic,
                        bool* p_had_error,
                        const bool p_is_eof,
                        const ok_source* p_source,
                        const token* p_token,
                        const report_severity p_severity,
                        const ok_string_view p_message) {
  return report_at_noted(p_panic, p_had_error, p_is_eof, p_source, p_token, p_severity, p_message, NULL);
}

// the token-based wrapper the parser and compiler ride. the renderer takes
// spans (offsets computed from pointers at token creation. never the old
// accumulated counter) and reads the source text itself, so the printed
// "line" is the source line, not the token text, and columns line up with it.
report_status report_at_noted(bool* p_panic,
                              bool* p_had_error,
                              const bool p_is_eof,
                              const ok_source* p_source,
                              const token* p_token,
                              const report_severity p_severity,
                              const ok_string_view p_message,
                              const ok_note* p_note) {
  // TODO: custom loggers
  const ok_span span = ok_span_of_token(p_token);
  return okdiag_report(p_panic,
                       p_had_error,
                       p_source != NULL ? p_source->path : "<unknown>",
                       p_source != NULL ? p_source->code : NULL,
                       &span,
                       p_is_eof,
                       p_severity,
                       p_message,
                       p_note);
}
