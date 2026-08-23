#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

string asprint(const char* p_fmt, ...) {
  // TODO this double copies make string accept taking ownership of already created buffer
  int n = 0;
  size_t size = 0;
  char* chars = NULL;
  va_list ap;

  va_start(ap, p_fmt);
  n = vsnprintf(chars, size, p_fmt, ap);
  va_end(ap);

  if (n < 0) {
    return create_string(NULL, 0, false);
  }

  size = (size_t)n + 1;
  chars = malloc(size);
  if (chars == NULL) {
    return create_string(NULL, 0, false);
  }

  va_start(ap, p_fmt);
  n = vsnprintf(chars, size, p_fmt, ap);
  va_end(ap);

  if (n < 0) {
    free(chars);
    return create_string(NULL, 0, false);
  }

  string str = create_string(chars, n, true);
  free(chars);
  return str;
}

report_status report_at(bool* p_panic,
                        bool* p_had_error,
                        const bool p_is_eof,
                        const source* p_source,
                        const token* p_token,
                        const report_severity p_severity,
                        const string_view p_message) {
  return report_at_noted(
      p_panic, p_had_error, p_is_eof, p_source, p_token, p_severity, p_message, create_string_view(NULL, 0, false));
}

report_status report_at_noted(bool* p_panic,
                              bool* p_had_error,
                              const bool p_is_eof,
                              const source* p_source,
                              const token* p_token,
                              const report_severity p_severity,
                              const string_view p_message,
                              const string_view p_note) {
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
                         (int)string_view_get_length(&p_message),
                         p_message.chars) < 0;
  if (p_is_eof) {
    printf_fail |= fprintf(stderr, "    | ") < 0;
    printf_fail |= fprintf(stderr, "at end (EOF).\n") < 0;
  } else {
    printf_fail |= fprintf(stderr, "    | ") < 0;
    printf_fail |= fprintf(stderr, "%.*s\n", p_token->length, p_token->start) < 0;
    if (p_token->type != TOKEN_ERROR) {
      printf_fail |= fprintf(stderr, "    | ") < 0;
      printf_fail |= fprintf(stderr, "%*c\n", p_token->line_info.offset - 1, '^') < 0;
    }
  }
  if (p_note.chars != NULL) {
    printf_fail |= fprintf(stderr, "    | note: %.*s\n", (int)string_view_get_length(&p_note), p_note.chars) < 0;
  }
  return printf_fail ? REPORT_STATUS_FAILED : REPORT_STATUS_OK;
}
