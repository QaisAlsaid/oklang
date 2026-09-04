#ifndef OK_SOURCE_H
#define OK_SOURCE_H

#include <stdint.h>

typedef enum {
  OK_FROM_FILE,
  OK_FROM_REPL,
} ok_from;

typedef struct {
  ok_from from;
  const char* code;
  const char* path;
} ok_source;

typedef struct {
  uint32_t offset;
  uint32_t line;
} ok_line_info;

typedef struct {
  ok_line_info line_info;
  uint32_t reps;
} ok_line_info_repeated;

#endif // OK_SOURCE_H
