#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "okdiag.h"

// the source line containing p_offset. returns the line's bounds; the caret
// column comes from the span (precomputed by the lexer).
static void line_bounds_of(const char* p_source, const ok_span* p_span, uint32_t* p_line_start, uint32_t* p_line_end) {
  uint32_t line_start = p_span->offset;
  while (line_start > 0 && p_source[line_start - 1] != '\n') {
    line_start--;
  }
  uint32_t line_end = p_span->offset;
  while (p_source[line_end] != '\0' && p_source[line_end] != '\n') {
    line_end++;
  }
  *p_line_start = line_start;
  *p_line_end = line_end;
}

ok_span ok_span_of_token(const token* p_token) {
  ok_span span;
  span.line = p_token->line_info.line;
  span.column = p_token->line_info.column;
  span.offset = p_token->line_info.offset;
  // TOKEN_ERROR's .start is the message (create_error_token), not source: the
  // old renderer printed the message as if it were the line. one caret on
  // the offending byte instead, the offset is already right.
  span.length = p_token->type == TOKEN_ERROR ? 1 : p_token->length;
  return span;
}

void okdiag_print_header(
    const char* p_path, uint32_t p_line, uint32_t p_column, const char* p_kind, const char* p_text) {
  fprintf(stderr, "%s:%u:%u: %s: %s", p_path, p_line, p_column, p_kind, p_text);
}

// `    NNN | the source line` + `        | ^~~~~`
// p_width is the shared line-number gutter width so a diagnostic and its
// notes align. returns false on printf failure (the caller folds it into the
// report status).
static bool print_snippet(const char* p_source, const ok_span* p_span, uint32_t p_width) {
  uint32_t line_start = 0;
  uint32_t line_end = 0;
  line_bounds_of(p_source, p_span, &line_start, &line_end);
  if (fprintf(stderr, "    %*u | ", (int)p_width, p_span->line) < 0) {
    return false;
  }
  if (fwrite(p_source + line_start, 1, line_end - line_start, stderr) < line_end - line_start) {
    return false;
  }
  if (fputc('\n', stderr) == EOF) {
    return false;
  }
  if (fprintf(stderr, "    %*s | ", (int)p_width, "") < 0) {
    return false;
  }
  // a tab counts as one column and renders raw. the caret aligns to the raw
  // text, which is the only thing that stays true on every terminal.
  for (uint32_t i = 1; i < p_span->column; ++i) {
    if (fputc(' ', stderr) == EOF) {
      return false;
    }
  }
  // the caret underlines the span's full length, clamped to the line, min one
  uint32_t carets = p_span->length;
  if (carets > line_end - p_span->offset) {
    carets = line_end - p_span->offset;
  }
  if (carets == 0) {
    carets = 1;
  }
  for (uint32_t i = 0; i < carets; ++i) {
    if (fputc('^', stderr) == EOF) {
      return false;
    }
  }
  return fputc('\n', stderr) != EOF;
}

static uint32_t digits_of(uint32_t p_value) {
  uint32_t digits = 1;
  while (p_value >= 10) {
    p_value /= 10;
    digits++;
  }
  return digits;
}

report_status okdiag_report(bool* p_panic,
                            bool* p_had_error,
                            const char* p_path,
                            const char* p_source,
                            const ok_span* p_span,
                            bool p_is_eof,
                            report_severity p_severity,
                            const ok_string_view p_message,
                            const ok_note* p_note) {
  if (*p_panic) {
    return REPORT_STATUS_SKIPPED;
  }
  bool printf_fail = false;
  *p_panic = true;
  *p_had_error = true;
  const char* kind = p_severity == REPORT_SEVERITY_WARN ? "warning" : "error";

  // a real span always has line >= 1. with source text: header + snippet.
  // without: located header only (right coordinates, no excerpt).
  // no span at all: bare `error: message`, like the no-location lanes.
  const ok_span* span = p_span != NULL && p_span->line > 0 ? p_span : NULL;

  uint32_t width = 1;
  if (span != NULL) {
    width = digits_of(span->line);
  }
  if (p_note != NULL && p_note->has_span && p_note->span.line > 0) {
    const uint32_t note_width = digits_of(p_note->span.line);
    if (note_width > width) {
      width = note_width;
    }
  }

  // header. EOF spans point one past the last byte and say so, on the message
  // line (gcc's placement), instead of rendering nothing.
  if (span != NULL) {
    printf_fail |= fprintf(stderr,
                           "%s:%u:%u: %s: %.*s",
                           p_path,
                           span->line,
                           span->column,
                           kind,
                           (int)ok_string_view_get_length(&p_message),
                           p_message.chars) < 0;
  } else {
    printf_fail |= fprintf(stderr, "%s: %.*s", kind, (int)ok_string_view_get_length(&p_message), p_message.chars) < 0;
  }
  if (p_is_eof) {
    printf_fail |= fprintf(stderr, " (at end of file)") < 0;
  }
  printf_fail |= fputc('\n', stderr) == EOF;

  // snippet (only with the source text alive)
  if (span != NULL && p_source != NULL) {
    printf_fail |= !print_snippet(p_source, span, width);
  }

  // note
  if (p_note != NULL && p_note->text.chars != NULL) {
    if (p_note->has_span && p_note->span.line > 0) {
      printf_fail |= fprintf(stderr,
                             "%s:%u:%u: note: %.*s\n",
                             p_path,
                             p_note->span.line,
                             p_note->span.column,
                             (int)ok_string_view_get_length(&p_note->text),
                             p_note->text.chars) < 0;
      if (p_source != NULL) {
        printf_fail |= !print_snippet(p_source, &p_note->span, width);
      }
    } else {
      printf_fail |= fprintf(stderr,
                             "    %*s | note: %.*s\n",
                             (int)width,
                             "",
                             (int)ok_string_view_get_length(&p_note->text),
                             p_note->text.chars) < 0;
    }
  }
  return printf_fail ? REPORT_STATUS_FAILED : REPORT_STATUS_OK;
}
