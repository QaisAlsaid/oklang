// the terminal layer (P5, the REPL round): raw-mode line editing with
// history, arrows, ctrl-left/right word jumps, and utf-8-aware editing.
// the whole file is the posix isolation zone - see term.h. on a non-tty
// stdin every read is a plain fgets, and none of this machinery runs.

#define _POSIX_C_SOURCE 200809L // termios, isatty, read

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#include "term.h"

// ---------------------------------------------------------------- state

static bool tty_is_interactive(void) {
  return isatty(STDIN_FILENO) == 1;
}

static struct termios g_cooked; // the terminal we owe back
static bool g_raw = false;

static void enter_raw(void) {
  if (!tty_is_interactive()) {
    return;
  }
  struct termios raw;
  if (tcgetattr(STDIN_FILENO, &g_cooked) != 0) {
    return;
  }
  raw = g_cooked;
  raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG); // we echo, we edit, we take ctrl-c
  raw.c_iflag &= (tcflag_t)~(IXON);                  // ctrl-s/q are ours while editing
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
    g_raw = true;
  }
}

static void leave_raw(void) {
  if (g_raw) {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_cooked);
    g_raw = false;
  }
}

// ---------------------------------------------------------------- history

static void history_push(term_session* p_term, const char* p_line) {
  if (p_line[0] == '\0') {
    return; // empty lines are not worth remembering
  }
  if (p_term->history_count > 0) {
    const uint32_t last = p_term->history_count - 1;
    if (p_term->history_count <= TERM_HISTORY_CAP && strcmp(p_term->history[last], p_line) == 0) {
      return; // an immediate repeat is one entry
    }
  }
  char* copy = strdup(p_line);
  if (copy == NULL) {
    return; // history is a nicety, never a wall
  }
  if (p_term->history_count == TERM_HISTORY_CAP) {
    free(p_term->history[0]);
    memmove(&p_term->history[0], &p_term->history[1], sizeof(char*) * (TERM_HISTORY_CAP - 1));
    p_term->history_count--;
  }
  p_term->history[p_term->history_count++] = copy;
}

// ---------------------------------------------------------------- rendering

// one codepoint == one column for editing purposes (combining marks are
// beyond this layer's contract). the caret math walks codepoints.
static size_t cp_count(const char* p_text, size_t p_bytes) {
  size_t count = 0;
  for (size_t i = 0; i < p_bytes;) {
    if (((unsigned char)p_text[i] & 0xC0) != 0x80) {
      count++;
    }
    i++;
  }
  return count;
}

// full redraw: prompt + buffer, then park the cursor. simple, robust to any
// edit; the buffer is human-sized, the terminal can take it.
static void redraw(const char* p_prompt, const char* p_buffer, size_t p_cursor) {
  fputs("\r", stdout);
  fputs(p_prompt, stdout);
  fputs(p_buffer, stdout);
  fputs("\x1B[K", stdout); // clear whatever the deleted tail left behind
  const size_t total = cp_count(p_buffer, strlen(p_buffer));
  const size_t cursor = cp_count(p_buffer, p_cursor);
  if (cursor < total) {
    // walk the caret left to its column (the redraw left it at the end)
    printf("\x1B[%zuD", total - cursor);
  }
  fflush(stdout);
}

// ---------------------------------------------------------------- the editor

// the word jump's word: a run of identifier bytes, or a run of anything
// else - the caret lands at the far side of whatever it is standing on.
static bool is_word_byte(char p_c) {
  const unsigned char u = (unsigned char)p_c;
  return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_' || u >= 0x80;
}

static size_t step_left(const char* p_buffer, size_t p_cursor) {
  if (p_cursor == 0) {
    return 0;
  }
  size_t at = p_cursor - 1;
  while (at > 0 && ((unsigned char)p_buffer[at] & 0xC0) == 0x80) {
    at--; // walk the continuation bytes back to the lead
  }
  return at;
}

static size_t step_right(const char* p_buffer, size_t p_cursor) {
  if (p_buffer[p_cursor] == '\0') {
    return p_cursor;
  }
  size_t at = p_cursor + 1;
  while (p_buffer[at] != '\0' && ((unsigned char)p_buffer[at] & 0xC0) == 0x80) {
    at++;
  }
  return at;
}

static bool read_edit(term_session* p_term, const char* p_prompt, char* p_buffer, size_t p_capacity) {
  p_buffer[0] = '\0';
  size_t length = 0;
  size_t cursor = 0;
  bool have_history = p_term->history_count > 0;
  for (;;) {
    redraw(p_prompt, p_buffer, cursor);
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) {
      p_term->exit_requested = true;
      return false;
    }
    if (c == '\r' || c == '\n') {
      fputs("\n", stdout);
      fflush(stdout);
      p_term->history_cursor = 0;
      free(p_term->saved_edit);
      p_term->saved_edit = NULL;
      history_push(p_term, p_buffer);
      return true;
    }
    if (c == 0x7F || c == '\b') { // backspace: a whole codepoint goes
      if (cursor > 0) {
        const size_t start = step_left(p_buffer, cursor);
        memmove(p_buffer + start, p_buffer + cursor, length - cursor + 1);
        length -= cursor - start;
        cursor = start;
      }
      continue;
    }
    if (c == 0x03) { // ctrl-c: empty line leaves, a draft clears
      if (length == 0) {
        fputs("^C\n", stdout);
        fflush(stdout);
        p_term->exit_requested = true;
        return false;
      }
      p_buffer[0] = '\0';
      length = 0;
      cursor = 0;
      continue;
    }
    if (c == 0x04) { // ctrl-d: leaves on an empty line, ignored mid-draft
      if (length == 0) {
        fputs("\n", stdout);
        fflush(stdout);
        p_term->exit_requested = true;
        return false;
      }
      continue;
    }
    if (c == 0x01) { // ctrl-a: home
      cursor = 0;
      continue;
    }
    if (c == 0x05) { // ctrl-e: end
      cursor = length;
      continue;
    }
    if (c == 0x17) { // ctrl-u: clear the draft
      p_buffer[0] = '\0';
      length = 0;
      cursor = 0;
      continue;
    }
    if (c == '\x1B') { // an escape sequence
      char seq[5];
      seq[0] = '\0';
      if (read(STDIN_FILENO, &seq[0], 1) != 1) {
        continue;
      }
      bool ctrl = false;
      if (seq[0] == '[') {
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
          continue;
        }
        if (seq[1] == '1' && read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == ';') {
          // ctrl-modified: "[1;5" then the letter
          char skip;
          ctrl = true;
          if (read(STDIN_FILENO, &skip, 1) != 1) {
            continue;
          }
          if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            continue;
          }
        }
      }
      const char key = seq[1];
      if (key == 'D') { // left (ctrl = the word jump)
        if (ctrl) {
          size_t at = step_left(p_buffer, cursor);
          if (at > 0 && is_word_byte(p_buffer[at])) {
            // a word run: land on its first byte
            while (at > 0 && is_word_byte(p_buffer[step_left(p_buffer, at)])) {
              at = step_left(p_buffer, at);
            }
          } else {
            // a non-word run (punctuation, spaces): land on its first byte
            while (at > 0 && !is_word_byte(p_buffer[step_left(p_buffer, at)])) {
              at = step_left(p_buffer, at);
            }
          }
          cursor = at;
        } else {
          cursor = step_left(p_buffer, cursor);
        }
      } else if (key == 'C') { // right
        cursor = step_right(p_buffer, cursor);
      } else if (key == 'A') { // up: back through history
        if (have_history && p_term->history_cursor < p_term->history_count) {
          if (p_term->history_cursor == 0) {
            free(p_term->saved_edit);
            p_term->saved_edit = strdup(p_buffer);
          }
          p_term->history_cursor++;
          const char* line = p_term->history[p_term->history_count - p_term->history_cursor];
          strncpy(p_buffer, line, p_capacity - 1);
          p_buffer[p_capacity - 1] = '\0';
          length = strlen(p_buffer);
          cursor = length;
        }
      } else if (key == 'B') { // down: forward through history
        if (p_term->history_cursor > 0) {
          p_term->history_cursor--;
          const char* line = p_term->history_cursor == 0 ? (p_term->saved_edit != NULL ? p_term->saved_edit : "")
                                                         : p_term->history[p_term->history_count - p_term->history_cursor];
          strncpy(p_buffer, line, p_capacity - 1);
          p_buffer[p_capacity - 1] = '\0';
          length = strlen(p_buffer);
          cursor = length;
        }
      }
      // everything else (home/end/delete/whatever) is politely ignored
      continue;
    }
    if ((unsigned char)c < 0x20) {
      continue; // the unassigned control bytes have no business here
    }
    if (length + 1 < p_capacity) {
      memmove(p_buffer + cursor + 1, p_buffer + cursor, length - cursor + 1);
      p_buffer[cursor] = c;
      cursor++;
      length++;
    }
  }
}

// ---------------------------------------------------------------- the door

void term_init(term_session* p_term) {
  p_term->raw = tty_is_interactive();
  p_term->exit_requested = false;
  memset(p_term->history, 0, sizeof(p_term->history));
  p_term->history_count = 0;
  p_term->history_cursor = 0;
  p_term->saved_edit = NULL;
  enter_raw();
}

void term_deinit(term_session* p_term) {
  leave_raw();
  for (uint32_t i = 0; i < p_term->history_count; ++i) {
    free(p_term->history[i]);
  }
  free(p_term->saved_edit);
  p_term->saved_edit = NULL;
  p_term->history_count = 0;
}

bool term_read_line(term_session* p_term, const char* p_prompt, char* p_buffer, size_t p_capacity) {
  if (!p_term->raw || !g_raw) {
    // the piped/degraded lane: fgets, no prompt, no echo (the pipe's writer
    // owns the bytes; a prompt would corrupt every comparison)
    for (;;) {
      if (fgets(p_buffer, (int)p_capacity, stdin) == NULL) {
        p_term->exit_requested = true;
        return false;
      }
      const size_t length = strlen(p_buffer);
      if (length == 0 || p_buffer[length - 1] != '\n') {
        return true; // a last line without its newline, or a quiet empty
      }
      p_buffer[length - 1] = '\0';
      if (length >= 2 && p_buffer[length - 2] == '\r') {
        p_buffer[length - 2] = '\0';
      }
      return true;
    }
  }
  return read_edit(p_term, p_prompt, p_buffer, p_capacity);
}
