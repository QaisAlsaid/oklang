#ifndef OK_SOURCE_H
#define OK_SOURCE_H

#include <stdint.h>

typedef enum {
  FROM_FILE,
  FROM_REPL,
} sourced;

typedef struct {
  sourced source;
  const char* code;
  const char* path;
} source;

typedef struct {
  uint32_t offset;
  uint32_t line;
} line_info;

typedef struct {
  line_info line_info;
  uint32_t reps;
} line_info_repeated;

#endif // OK_SOURCE_H
