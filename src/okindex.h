#ifndef OK_INDEX_H
#define OK_INDEX_H

#include <stddef.h>
#include <stdint.h>

// fuck strings.h
typedef uint16_t ok_index;

#define INDEX_MAX UINT16_MAX
#define IS_INDEX_VALID(iw) ((iw.errors) == INDEX_OK)

typedef enum { INDEX_OK = 0 } index_error;

typedef struct {
  ok_index index;
  union {
    uint8_t flags;
    uint8_t kind;
    uint8_t type;
  };
  union {
    uint8_t errors;
    uint8_t error;
  };
} index_wrapper;

#endif // OK_INDEX_H
