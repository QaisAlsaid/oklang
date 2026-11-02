#ifndef OK_SOURCE_H
#define OK_SOURCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// the source handed to ok_run. ok reads both fields during the call.
// your buffers are yours. the path is interned (copied) internally
// so diagnostics can name it after the run.
// the code is copied too, but only when source retention is on (ok_retain_source).
typedef struct {
  const char* code;
  const char* path;
} ok_source;

// a location in the source. `offset` is the ablsolute file byte offset
// of the token start and `column` its 1-based column,
// the column is stored here so runtime reports can
// print `path:line:col` even after the source text itself is gone.
// current asuumption: char = byte
typedef struct {
  uint32_t offset;
  uint32_t line;
  uint32_t column;
} ok_line_info;

typedef struct {
  ok_line_info line_info;
  uint32_t reps;
} ok_line_info_repeated;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // OK_SOURCE_H
