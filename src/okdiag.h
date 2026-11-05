#ifndef OK_DIAG_H
#define OK_DIAG_H

#include <stdbool.h>
#include <stdint.h>

#include "ok/ok_source.h"
#include "okstring.h"
#include "oktoken.h"

// the diagnostics renderer. parse errors, compile errors and runtime reports
// all print through here (gcc-parseable `path:line:col:` headers plus a
// rustc-style source snippet), so the format is a contract, not an accident.

typedef enum {
  REPORT_SEVERITY_WARN,
  REPORT_SEVERITY_ERROR,
} report_severity;

typedef enum {
  REPORT_STATUS_OK,
  REPORT_STATUS_FAILED,
  REPORT_STATUS_SKIPPED,
} report_status;

// a byte span into the source. computed by pointer arithmetic at token
// creation, never by an accumulated counter. that was the old R1 bug. the
// column rides along (precomputed by the lexer) so headers can render even
// without the source text (expired borrow: right coordinates, no excerpt).
typedef struct {
  uint32_t line;   // 1-based, straight from the lexer's line counter
  uint32_t column; // 1-based, byte = char, tab = one column
  uint32_t offset; // file-absolute byte offset of the span start
  uint32_t length; // span length in bytes; 0 renders one caret (EOF)
} ok_span;

// a note with an optional location: text alone renders as today's notes
// (`    | note: text`), text + span renders a full secondary diagnostic with
// its own header and snippet ("note: previously declared here").
typedef struct {
  ok_string_view text;
  bool has_span;
  ok_span span;
} ok_note;

// the span of a token. TOKEN_ERROR tokens carry the message in .start (the
// parser reads it from there), so their span length is forced to the single
// offending byte instead of the message's length.
ok_span ok_span_of_token(const token* p_token);

// renders one diagnostic. p_source may be NULL: located header, no
// snippet (right coordinates, no excerpt, degraded).
// p_span may be NULL: bare `error: message`, like the no-location lanes.
// p_note may be NULL. first error wins: *p_panic gates
// (REPORT_STATUS_SKIPPED), matching the v1 panic-mode ruling.
report_status okdiag_report(bool* p_panic,
                            bool* p_had_error,
                            const char* p_path,
                            const char* p_source,
                            const ok_span* p_span,
                            bool p_is_eof,
                            report_severity p_severity,
                            const ok_string_view p_message,
                            const ok_note* p_note);

// the diagnostics sink (P5): when installed, every rendered diagnostic
// routes here INSTEAD of stderr - the rendering is replaced, not merely
// forwarded. the sink owns the panic/had_error contract (set both, the
// first report wins otherwise). consumers today: the fuzz harness (validates
// the every-diagnostic-carries-a-location bar over hostile input) and the
// REPL's shape-sniff (runs its trial parse silenced). internal lane, not
// public api - hosts keep stderr.
typedef report_status (*okdiag_sink)(bool* p_panic,
                                     bool* p_had_error,
                                     const char* p_path,
                                     const char* p_source,
                                     const ok_span* p_span,
                                     bool p_is_eof,
                                     report_severity p_severity,
                                     const ok_string_view p_message,
                                     const ok_note* p_note);
void okdiag_install_sink(okdiag_sink p_sink);

// the (path, line, column) header every diagnostic starts with, exposed for
// the runtime report's stack trace lines.
void okdiag_print_header(
    const char* p_path, uint32_t p_line, uint32_t p_column, const char* p_kind, const char* p_text);

#endif // OK_DIAG_H
