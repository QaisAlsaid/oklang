// the REPL (P5, the front-door patch's last round): buffer accumulation
// with nesting-aware continuation, auto-print of lone expressions with ';'
// suppression, and the exit contract. the tty editor (raw mode, history,
// word jumps) lives in term.c - this file is the semantics, and it stays
// pure c99 so the eventual portability lane never touches it.
//
// the auto-print trick: a completed buffer that does not end in ';' is
// trial-parsed as `print((\n<buffer>\n));` with the diagnostics silenced;
// if that parses, the wrapped form RUNS (the language's own print renders
// the value - one renderer, no host-side formatting drift). anything else
// runs raw. a wrapped parse failure falls back to the raw buffer, whose
// diagnostics are the real ones. the cost: runtime errors inside an
// auto-printed expression report one line lower than the input (the
// wrapper's own first line). noted, accepted, pinned by fixture.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ok/ok.h"
#include "okdiag.h"
#include "okparser.h"
#include "okstate.h"

#include "term.h"

// the accumulation cap: a paste the size of a program is a file's job
#define REPL_BUFFER_MAX (1024u * 1024u)

// ---------------------------------------------------------------- the continuation walker

// does the buffer want another line? mirrors the scanner's rules: line
// comments die at newline; an escape never closes anything; a plain string
// cannot span lines (an unterminated one is a COMPLETE buffer - its error
// is honest); an m-string can; a ${} region nests and can hold strings of
// its own; brackets outside all of that count.
typedef enum {
  WALK_CODE,
  WALK_LINE_COMMENT,
  WALK_STRING,
  WALK_REGION, // inside a ${...}: brackets count, strings open
} walk_state;

typedef struct {
  walk_state state;
  char quote;        // the string's own quote byte
  bool multiline;    // m or fm prefix
  bool interpolate;  // f or fm prefix
  bool escaped;      // the previous byte was a backslash
  uint32_t region_depth;
  int32_t brackets;  // () [] {} in code, {} in regions
} walker;

static bool is_ident_byte(char p_c) {
  const unsigned char u = (unsigned char)p_c;
  return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_';
}

// the quote's prefix rules, mirrored from the lexer: f/m stick to the quote
// when the byte before THEM is not an identifier byte (so `xf"` is the
// identifier `xf` then a PLAIN string - still a string, just not prefixed).
static void string_prefixes(const char* p_text, size_t p_at, bool* p_multiline, bool* p_interpolate) {
  *p_multiline = false;
  *p_interpolate = false;
  if (p_at == 0) {
    return;
  }
  const char prev = p_text[p_at - 1];
  if (prev == 'f' && (p_at < 2 || !is_ident_byte(p_text[p_at - 2]))) {
    *p_interpolate = true;
  }
  if (prev == 'm' && (p_at < 2 || !is_ident_byte(p_text[p_at - 2]))) {
    *p_multiline = true;
  }
  if (prev == 'm' && p_at >= 2 && p_text[p_at - 2] == 'f' && (p_at < 3 || !is_ident_byte(p_text[p_at - 3]))) {
    *p_interpolate = true; // fm": both flags
  }
}

static void walker_init(walker* p_walker) {
  p_walker->state = WALK_CODE;
  p_walker->quote = 0;
  p_walker->multiline = false;
  p_walker->interpolate = false;
  p_walker->escaped = false;
  p_walker->region_depth = 0;
  p_walker->brackets = 0;
}

// one byte. returns false the moment the buffer is known-incomplete-in-a-
// way-only-a-newline-can-fix... which never happens mid-line; the answer
// is only meaningful at end-of-buffer. (kept a function for shape.)
static void walker_step(walker* p_walker, const char* p_text, size_t p_at, char p_c) {
  switch (p_walker->state) {
  case WALK_LINE_COMMENT:
    if (p_c == '\n') {
      p_walker->state = p_walker->region_depth > 0 ? WALK_REGION : WALK_CODE;
    }
    return;
  case WALK_STRING:
    if (p_walker->escaped) {
      p_walker->escaped = false; // the escaped byte never closes, never opens
      return;
    }
    if (p_c == '\\') {
      p_walker->escaped = true;
      return;
    }
    if (p_c == p_walker->quote) {
      p_walker->state = p_walker->region_depth > 0 ? WALK_REGION : WALK_CODE;
      return;
    }
    if (p_walker->interpolate && p_c == '$' && p_text[p_at + 1] == '{') {
      p_walker->state = WALK_REGION;
      p_walker->region_depth = 1; // the '${' itself opened one
      p_walker->escaped = false;
    }
    return;
  case WALK_REGION:
    if (p_c == '{') {
      p_walker->brackets++;
      return;
    }
    if (p_c == '}') {
      if (p_walker->brackets > 0) {
        p_walker->brackets--;
        return;
      }
      // the region's own closer
      p_walker->region_depth--;
      if (p_walker->region_depth == 0) {
        p_walker->state = WALK_STRING;
      }
      return;
    }
    if (p_c == '"' || p_c == '\'') {
      string_prefixes(p_text, p_at, &p_walker->multiline, &p_walker->interpolate);
      p_walker->quote = p_c;
      p_walker->escaped = false;
      p_walker->state = WALK_STRING;
      return;
    }
    if (p_c == '/' && p_text[p_at + 1] == '/') {
      p_walker->state = WALK_LINE_COMMENT;
      return;
    }
    return;
  case WALK_CODE:
    if (p_c == '/' && p_text[p_at + 1] == '/') {
      p_walker->state = WALK_LINE_COMMENT;
      return;
    }
    if (p_c == '"' || p_c == '\'') {
      string_prefixes(p_text, p_at, &p_walker->multiline, &p_walker->interpolate);
      p_walker->quote = p_c;
      p_walker->escaped = false;
      p_walker->state = WALK_STRING;
      return;
    }
    if (p_c == '(' || p_c == '[' || p_c == '{') {
      p_walker->brackets++;
      return;
    }
    if (p_c == ')' || p_c == ']' || p_c == '}') {
      p_walker->brackets--;
      return;
    }
    return;
  }
}

// the buffer's completeness at end-of-input for this round: a plain string
// meeting its newline is complete (the parse will say unterminated,
// honestly); a multiline one, an open region, or open brackets want more.
static bool buffer_complete(const char* p_buffer, size_t p_length) {
  walker walk;
  walker_init(&walk);
  for (size_t i = 0; i < p_length; ++i) {
    walker_step(&walk, p_buffer, i, p_buffer[i]);
  }
  if (walk.state == WALK_STRING && !walk.multiline) {
    return true; // the unterminated-string error is the honest answer
  }
  if (walk.state == WALK_STRING || walk.state == WALK_REGION || walk.brackets > 0) {
    return false; // the m-string, the region, the brackets: keep reading
  }
  return true;
}

// the last meaningful byte: whitespace and whole trailing comments do not
// count (a ';' inside a comment never suppresses anything). the walker is
// the judge - the same rules the scanner lives by.
static char last_meaningful(const char* p_buffer, size_t p_length) {
  walker walk;
  walker_init(&walk);
  char last = '\0';
  for (size_t i = 0; i < p_length; ++i) {
    const char c = p_buffer[i];
    const bool in_code = walk.state == WALK_CODE || walk.state == WALK_REGION;
    if (in_code && c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      last = c;
    }
    walker_step(&walk, p_buffer, i, c);
  }
  return last;
}

// ---------------------------------------------------------------- the shape sniff

// the silence: the trial parse reports nowhere (the real diagnostics come
// from whichever buffer actually runs)
static report_status quiet_sink(bool* p_panic,
                                bool* p_had_error,
                                const char* p_path,
                                const char* p_source,
                                const ok_span* p_span,
                                bool p_is_eof,
                                report_severity p_severity,
                                const ok_string_view p_message,
                                const ok_note* p_note) {
  (void)p_path;
  (void)p_source;
  (void)p_span;
  (void)p_is_eof;
  (void)p_severity;
  (void)p_message;
  (void)p_note;
  *p_panic = true;
  *p_had_error = true;
  return REPORT_STATUS_OK;
}

// does the wrapped form parse? (the wrap tolerates the missing ';' the raw
// buffer would refuse - that tolerance IS the auto-print lane)
static bool parses_wrapped(ok* p_ok, const char* p_wrapped) {
  ok_source source;
  source.code = (char*)p_wrapped;
  source.path = "stdin";
  parser parser;
  parser_specs specs;
  specs.alloc = &p_ok->alloc;
  specs.source = &source;
  okdiag_install_sink(quiet_sink);
  parser_init(&parser, specs);
  parse_result parsed = parser_parse(&parser);
  const bool ok_shape = parsed.status == PARSE_OK;
  parse_result_deinit(&parsed);
  okdiag_install_sink(NULL);
  return ok_shape;
}

// ---------------------------------------------------------------- the loop

static void run_buffer(ok* p_ok, const char* p_code, int32_t* p_exit_code, bool* p_exited) {
  ok_source source;
  source.code = (char*)p_code;
  source.path = "stdin";
  const ok_result result = ok_run(p_ok, source);
  if (result == OK_EXITED) {
    // the script asked to stop: the session ends with its code, the
    // instance still freed by the caller (the stub's exit() leaked it)
    *p_exit_code = ok_exit_code(p_ok);
    *p_exited = true;
  }
  // every other outcome already spoke through the diagnostics renderer;
  // the repl's job is to keep breathing
}

void repl_run(ok* p_ok, int32_t* p_exit_code) {
  *p_exit_code = 0;
  ok_retain_source(p_ok, true); // runtime reports keep their text across runs

  term_session term;
  term_init(&term);

  char line[4096];
  char* buffer = (char*)malloc(8192);
  size_t buffer_capacity = 8192;
  size_t buffer_length = 0;
  if (buffer == NULL) {
    fprintf(stderr, "repl: out of memory.\n");
    term_deinit(&term);
    return;
  }

  for (;;) {
    const char* prompt = buffer_length == 0 ? "> " : "... ";
    if (!term_read_line(&term, prompt, line, sizeof(line))) {
      // the stream ended, or the user asked to leave. a pending incomplete
      // buffer dies with the session - running it would be a guess
      if (term.raw) {
        fputs("\n", stdout); // a tty goodbye; the pipe never wanted one
      }
      break;
    }
    const size_t line_length = strlen(line);
    if (buffer_length + line_length + 2 > buffer_capacity) {
      const size_t fresh_capacity = buffer_capacity * 2;
      if (fresh_capacity > REPL_BUFFER_MAX) {
        fprintf(stderr, "repl: the buffer is full - a program this size wants a file.\n");
        buffer_length = 0;
        buffer[0] = '\0';
        continue;
      }
      char* fresh = (char*)realloc(buffer, fresh_capacity);
      if (fresh == NULL) {
        fprintf(stderr, "repl: out of memory.\n");
        break;
      }
      buffer = fresh;
      buffer_capacity = fresh_capacity;
    }
    memcpy(buffer + buffer_length, line, line_length);
    buffer_length += line_length;
    buffer[buffer_length++] = '\n';
    buffer[buffer_length] = '\0';

    if (!buffer_complete(buffer, buffer_length)) {
      continue; // the continuation prompt takes the next line
    }

    // nothing-but-air buffers (comments included) never run
    if (last_meaningful(buffer, buffer_length) == '\0') {
      buffer_length = 0;
      buffer[0] = '\0';
      continue;
    }

    // the evaluation: ';' suppresses the auto-print; everything else gets
    // the trial wrap
    if (last_meaningful(buffer, buffer_length) == ';') {
      bool exited = false;
      run_buffer(p_ok, buffer, p_exit_code, &exited);
      if (exited) {
        break;
      }
    } else {
      const size_t wrapped_capacity = buffer_length + 32;
      char* wrapped = (char*)malloc(wrapped_capacity);
      if (wrapped == NULL) {
        fprintf(stderr, "repl: out of memory.\n");
        break;
      }
      snprintf(wrapped, wrapped_capacity, "print((\n%s\n));", buffer);
      if (parses_wrapped(p_ok, wrapped)) {
        bool exited = false;
        run_buffer(p_ok, wrapped, p_exit_code, &exited);
        free(wrapped);
        if (exited) {
          break;
        }
      } else {
        free(wrapped);
        bool exited = false;
        run_buffer(p_ok, buffer, p_exit_code, &exited);
        if (exited) {
          break;
        }
      }
    }
    buffer_length = 0;
    buffer[0] = '\0';
  }

  free(buffer);
  term_deinit(&term);
}
