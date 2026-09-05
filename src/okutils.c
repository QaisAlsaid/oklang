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
  return report_at_noted(
      p_panic, p_had_error, p_is_eof, p_source, p_token, p_severity, p_message, ok_create_string_view(NULL, 0, false));
}

report_status report_at_noted(bool* p_panic,
                              bool* p_had_error,
                              const bool p_is_eof,
                              const ok_source* p_source,
                              const token* p_token,
                              const report_severity p_severity,
                              const ok_string_view p_message,
                              const ok_string_view p_note) {
  // TODO: custom loggers
  // idk man should we care about printf fail, or at least hide behind macro or not care at all..
  // TODO fix formmating, token has only relevant part to it, while error message require full line info (for proper
  // offset)
  if (*p_panic)
    return REPORT_STATUS_SKIPPED;
  bool printf_fail = false;
  *p_panic = true;
  *p_had_error = true;
  const char* report = p_severity == REPORT_SEVERITY_WARN ? "warning" : "error";
  printf_fail |= fprintf(stderr,
                         "%s:%d:%d: %s: %.*s\n",
                         p_source->path,
                         p_token->line_info.line,
                         p_token->line_info.offset,
                         report,
                         (int)ok_string_view_get_length(&p_message),
                         p_message.chars) < 0;
  if (p_is_eof) {
    printf_fail |= fprintf(stderr, "    | ") < 0;
    printf_fail |= fprintf(stderr, "at end (EOF).\n") < 0;
  } else {
    printf_fail |= fprintf(stderr, "    | ") < 0;
    printf_fail |= fprintf(stderr, "%.*s\n", p_token->length, p_token->start) < 0;
#if 0
    if (p_token->type != TOKEN_ERROR) {
      printf_fail |= fprintf(stderr, "    | ") < 0;
      printf_fail |= fprintf(stderr, "%*c\n", p_token->line_info.offset - 1, '^') < 0;
    }
#endif
  }
  if (p_note.chars != NULL) {
    printf_fail |= fprintf(stderr, "    | note: %.*s\n", (int)ok_string_view_get_length(&p_note), p_note.chars) < 0;
  }
  return printf_fail ? REPORT_STATUS_FAILED : REPORT_STATUS_OK;
}
