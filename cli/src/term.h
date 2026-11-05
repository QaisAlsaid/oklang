#ifndef OK_TERM_H
#define OK_TERM_H

// the terminal layer (P5, the REPL round). EVERYTHING posix lives here and
// only here - termios raw mode, isatty - so the eventual pure-c99 lane
// swaps this one file (or its callers degrade to fgets) and nothing else
// moves. the CLI is a host, not the library: this file never links into
// libok.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TERM_HISTORY_CAP 100

typedef struct {
  bool raw;                 // raw mode was entered (a tty owns us)
  bool exit_requested;      // ctrl-c on an empty line, or ctrl-d
  char* history[TERM_HISTORY_CAP]; // the ring, strdup'd lines
  uint32_t history_count;
  uint32_t history_cursor;  // 0 == "the live edit", N == history[N-1]
  char* saved_edit;         // the in-progress line parked while browsing
} term_session;

// enters the session. if stdin is not a tty (or termios refuses), the
// session degrades: reads fall back to plain fgets and none of the editing
// exists. prompts are the caller's business either way.
void term_init(term_session* p_term);
// restores the cooked terminal (if raw was entered) and frees the history.
void term_deinit(term_session* p_term);

// reads one line into p_buffer (nul-terminated, newline stripped).
// returns false when the stream ends or the user asked to leave
// (p_term->exit_requested says which). the prompt renders on the tty path
// only; the piped path stays byte-clean for comparisons.
bool term_read_line(term_session* p_term, const char* p_prompt, char* p_buffer, size_t p_capacity);

#endif // OK_TERM_H
